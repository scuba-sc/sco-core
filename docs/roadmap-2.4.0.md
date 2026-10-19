# `sco-core` SDK 2.4.0, Studio 0.2 & `sc-offline` 0.7.9 Ecosystem Roadmap

This is an unreleased target roadmap, not a release announcement. Feature presence,
provider availability, synthetic validation and in-game acceptance are separate
gates. No release or in-game verification is claimed here.

**Target Releases:**
* **`sco-core` SDK:** `v2.4.0` (Star Citizen Game Pack `v0.8.0`, Core Plugin ABI `v1.1` Frozen)
* **`scuba-engine` Studio:** `v0.2.0` (Native D3D12/ImGui Authoring Studio + MCP Bridge)
* **`sc-offline` Product:** `v0.7.9.0` (Unified 2.4.0 Plugin Suite + In-Game UI Restoration)
* **`sc-mp` Co-Presence:** `v0.1.0` (Standalone Windows/POSIX UDP Relay & Session Plugin)

**Pinned Game Baseline:** `4.10.197.2878`

**MCP Protocol Version:** `2025-11-25`

**License:** `GPL-3.0-only` (REUSE / SPDX compliant)

---

## 1. Executive Summary

The `v2.4.0` release cycle unifies all in-flight foundation branches, service expansions, studio tools, and community contributions into a single, cohesive ecosystem release:

1. **`sco-core` v2.4.0 (Game Pack v0.8.0)**: Targets a self-contained, agent-ready SDK across five distribution profiles. Includes `sco.widgets` 1.2 (stable automation tags), `sco.storage` 1.1 (SQLite schema introspection), `game.loadout` 1.2 (asynchronous equip + `SPI_Player_OnRespawnLoadoutCompleted` lifecycle event), `game.inventory` 1.1 + `sc_container_contents.h` (copied container snapshots + 192-member `EItemType` enum), `game.input` 1.0 (1,267-member `EGameActions` enum), `game.gamerules` 1.1 (`list_params`), 29 raw actor condition reads, host provider sockets (`SetRelocationBackend`, `SetLoadoutBackend`), `datacore.pack` (`BestEffort` receipts + `clone_mode = "deep"` + `sco-dcb check`), and `datacore.live` (`sc_datacore_live.h`).
2. **`scuba-engine` v0.2.0**: Combines the Native D3D12 + ImGui dockspace editor (PR `#21`), headless designer codegen (PR `#27`), mock actor stats (PR `#25`), bounded `project_build`/`project_check`/`project_test` MCP tools, `sco.widgets` 1.2 stable-tag readback, and full-product `sc-offline` profile loading so developers can build, inspect, and test the entire `sc-offline` plugin suite out-of-game.
3. **`sc-offline` v0.7.9.0**: Must resolve the `0.7.8.1` in-game UI focus, input routing, and callback dispatch regressions, and unifies all 13+ modular plugins (including rebased community PR `#127` gear attachment slots 8–21 and PR `#117` ASOP vehicle selector) onto the `sco-core` 2.4.0 SDK.
4. **`sc-mp` v0.1.0**: Targets integration of the standalone Windows/POSIX relay server and session harness (PR `#5`), verified across 132,422 multi-peer simulation and loopback UDP checks.

---

## 2. The Five `sco-core` 2.4.0 Distribution Profiles

Every `sco-core` release builds and verifies five deterministic archives (`SHA256SUMS`, `SBOM.spdx.json`, and `COMPATIBILITY.json`):

| Profile Name | Archive Filename | Target Consumer | Bundled Contents & Verification |
| :--- | :--- | :--- | :--- |
| **`core`** | `sco-sdk-core-2.4.0.zip` | Game-neutral plugin authors | Neutral kernel headers (`sco_*.h`), C++ wrappers, C# (`Sco.Sdk`, compiler-checked layout pins) & Lua bindings, `sco-new-plugin.py`, `sco-plugin-check`, CMake package config, and neutral examples (`widgets` 1.2, `cs_hello`). |
| **`game-sc`** | `sco-sdk-game-sc-0.8.0.zip` | Star Citizen plugin authors (overlay) | Composes over `core`. Adds `sc_*.h` / `scosdk/game/*.hpp` headers, compiler-built `sdk-catalogue.json`, `tools/sco-core-mcp/server.py`, native/status metadata, and `docs/sdk-developer-mvp.md`. |
| **`legacy` (Combined)** | `sco-sdk-2.4.0.zip` | All-in-one developer & AI agent kit | Complete self-contained kit combining `core` + `game-sc` + `sdk-catalogue.json` + stdio MCP discovery server + examples + checker. |
| **`core-source`** | `sco-sdk-core-source-2.4.0.zip` | Game-neutral host builders (`sc-mp`) | Repository-layout source tree for building a game-neutral host runtime (`src/host`, `src/widgets`, `src/storage`, `src/net`). Verified free of game-specific code by `test-zip.sh`. |
| **`host-source-sc`** | `sco-sdk-host-source-sc-2.4.0.zip` | Studio & product host builders (`scuba-engine`, `sc-offline`) | Full repository-layout source kit including `core-source`, `src/game`, `src/datacore`, `sdk-catalogue.json`, and stdio MCP discovery tooling. Verified via `test-zip.sh` synthetic host & game-pack CTest suite. |

---

## 3. Unified Feature Matrix in SDK 2.4.0 (Game Pack 0.8.0)

| Subsystem / Service | Version | Tracked Issues / PRs | Key Additions in 2.4.0 |
| :--- | :--- | :--- | :--- |
| **Agent MCP Discovery** | `2025-11-25` | PR `#226`, PR `#218` | Bundled `sdk-catalogue.json` + zero-dependency `tools/sco-core-mcp/server.py` (6 default + 4 extended tools: `list_services`, `get_capability_status`, `list_engine_natives`, `get_example`, `validate_manifest_and_widgets`, `generate_plugin_scaffold`). |
| **`sco.widgets`** | `v1.2` | `#142`, `#143`, `K-38` | `sco_widget_node_1_2` (`128` bytes, `const char* tag` at offset `104`, limited to 63 ASCII bytes, `SCO_WIDGETS_FEATURE_STABLE_TAGS = 0x80u`), C++/C#/Lua bindings, and `scuba-engine` JSON readback (`widgets.get` / `widgets.snapshot`). |
| **`sco.storage`** | `v1.1` | `K-38` | `144`-byte `sco_storage_v1` table adding bounded SQLite table/view/column schema introspection (`KIND_TABLE`, `KIND_VIEW`, `COLUMN_NOTNULL`, `COLUMN_PK`). |
| **`sco.diagnostics.publisher`** | `v1.0` | `#141`, `K-38` | Privacy-preserving numeric/enum observation contract (`sco_diagnostics_observation`). |
| **`game.loadout`** | `v1.2` | `#195`, PR `#221` | Asynchronous loadout equip requests, generation tracking, `SPI_Player_OnRespawnLoadoutCompleted` lifecycle event, and `sco_game_set_loadout_backend` host socket. |
| **`game.inventory` & `game.inventory.contents`** | `v1.1` / `v1.0` | `#190`, PR `#185`, PR `#226` | `sc_container_contents.h` copied-item snapshot ABI (`sc_container_contents_v1.snapshot`), `game.inventory` 1.1 adapter, and 192-member `EItemType` category enum + display-name lookup. |
| **`game.input`** | `v1.0` | `#194` | 1,267-member `EGameActions` enum and fast name/ID lookup table anchored to `4.10.197.2878`. |
| **`game.gamerules`** | `v1.1` | `#189` | `list_params` read-only parameter definitions registry (`SGameRulesCvars`). |
| **`game.actor_stats` & `game.actors`** | `v1.1` | PR `#226`, `K-12` | 29 guarded raw actor condition read IDs, pinned `1.0` write allowlist, and seated actor removal guard. |
| **`game.relocation` & `game.host`** | `v1.1` | `K-29`, `K-30` | `sco_game_set_relocation_backend` host provider socket and read-only `INetFwPolicy2` COM firewall rule query for G1 offline admission evidence. |
| **`datacore.pack` & `sco-dcb`** | `v1.1` | `#191`, `#192` | `PackApplyPolicy::BestEffort` + per-record `PackApplyReceipt` (keeping `Atomic` default), `clone_mode = "deep"` array cloning, and `sco-dcb check <pack.toml>` CLI validator. |
| **`datacore.live`** | `v1.0` | `#193`, PR `#217` | `sc_datacore_live.h` host backend seam (`read_pool`, `write_pool`, `get_pool_stats`) with owner/game-thread enforcement, default-off write opt-in, and fault latch. |

---

## 4. Ecosystem Convergence Milestones

```
[Milestone 1: Unify sco-core 2.4.0]
  └── Merge K-38 (widgets 1.2 / storage 1.1), Wave 0/1/2 (#185, #189-#195, K-29/K-30), and open PRs (#217-#221, #199, #188) into PR #226
  └── Verify 9/9 Hosted CI + 5 Archive Profiles + Close all superseded PRs & resolved Issues

[Milestone 2: Unify scuba-engine 0.2.0 & sc-mp 0.1.0]
  └── Merge scuba-engine Workspace v2 + K-38 Tag Readback + PR #21 (Native D3D12) + PR #25 (Mock Actor Stats) + PR #27 (Designer Codegen)
  └── Close stale WebUI bot PRs (#18, #3, etc.)
  └── Merge sc-mp PR #5 (Relay Server) & Close PR #1

[Milestone 3: Unify sc-offline 0.7.9.0 & Restore In-Game UI]
  └── Update sc-offline to SDK 2.4.0 & merge sc-offline-axiom-foundations
  └── Fix 0.7.8.1 D3D/ImGui focus, widget callback routing, and G1 admission regressions in src/
  └── Port PR #127 (Gear Attachments) & PR #117 (ASOP Selector) into modular 2.4.0 plugins
  └── Verify all 13+ plugins inside scuba-engine & close resolved sc-offline PRs/Issues
```
