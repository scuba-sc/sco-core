# sco-core documentation

Curated guides by audience, not an exhaustive inventory of historical records.
For the project overview, start with the [root README](../README.md).

## New plugin author — Start a plugin

1. [Start your first plugin](getting-started.md): choose language/archive,
   prerequisites, published download/checksum, copy/rename/build/check, Windows
   deployment and diagnostics in one path.
2. [SDK archive reference](../sdk/README.md), [plugin.ini](../sdk/docs/plugin-ini.md)
   and [plugin rules](../sdk/docs/plugin-rules.md): packaging, manifests and trust.
3. Your language: [C ABI](api-v1.md), [C++](sdk-cpp.md), [C#](sdk-csharp.md),
   [Lua](../sdk/docs/lua.md) or [data packs](../sdk/docs/data-packs.md).
4. [Designing modular plugins](plugin-design.md): one feature per plugin, versioned
   service tables, `requires`, clean unload and anti-patterns, with a provider and
   consumer example pair.
5. [Community plugin directory](plugin-directory.md): plugins in their authors' own
   repositories with reported SDK compatibility, the listing rules and how to request a
   listing. Not an endorsement; nothing there is verified in game.

## Plugin API consumer — Use the API safely

1. [Plugin API v1](api-v1.md): exports/root table, ABI compatibility, whole-member
   and non-null checks, capabilities, commands, events, raw handlers and threads.
   [Stability](stability.md) says which service tables are Stable or Experimental
   across SDK 2.x, separately from availability and evidence.
2. [Public contract catalogue and reading rules](contracts.md), backed by
   [machine-readable records](../sdk/contracts): per-method semantics, buffers,
   failures, ownership, callback/service lifetime and unspecified guarantees.
   Declarations come from [sco_api.h](../include/sco_api.h); the signature-row
   page [game/contracts.md](game/contracts.md) is **not** this contract authority.
3. [Plugin lifetime/fault/reload rules](plugins.md) and
   [native trust rules](../sdk/docs/plugin-rules.md): quiescence, Stop/unload and
   restart boundaries. [C++](sdk-cpp.md), [C#](sdk-csharp.md) and
   [Lua](../sdk/docs/lua.md) explain language-specific exposure and limits.
4. Neutral services: [storage](storage.md), [UI](ui.md), [IPC](ipc.md),
   [network messages](net.md) and [network wire](net-wire.md).
5. Optional extension: [DataCore](datacore.md), [game services](game-services.md)
   and [current evidence/status](game/status.md). Availability is not gameplay proof.

## SDK integrator / host — Embed a host

1. [Kernel source kit](../sdk/README.md#build-the-kernel-source-kit) versus
   [checkout build](building.md): platform/toolchain and neutral/game boundaries.
2. [Host/internal C++ API](api.md): `include/sco/`, `Platform`, Start/Tick/Stop;
   this is **not** the plugin C++ wrapper reference.
3. [Architecture](architecture.md): layers, startup, scheduling and limits.
4. [Plugin loader](plugins.md) and [Lua runtime host](../plugins/lua/README.md):
   dependency ordering, discovery, reload and native trust.
5. Host controls: [storage](storage.md), [UI](ui.md), [IPC](ipc.md),
   [network](net.md), [DataCore](datacore.md), [game-pack opt-in](game-services.md).
6. [Host simulator](building.md#sco-host-sim) and focused tests in
   [building/testing](building.md); [host lessons](framework.md#lessons).

## Game-pack contributor — Check or update a game build

1. [Game pack](game-pack.md) and [service/capability evidence](game/status.md):
   pack/version, checked signatures, pending runtime diagnostics and game acceptance.
2. [Sigcheck](sigcheck.md): setup, output, exit codes and patch-day workflow.
3. [Adding signatures](adding-signatures.md): row ownership, uniqueness and guards.
4. Feature inventories: [ASOP/hangar/ATC](game/asop.md), [world](game/world.md),
   [contracts](game/contracts.md), [actors](game/actors.md),
   [entities](game/entities.md), [events](game/events.md),
   [creative](game/creative.md), [mining](game/mining.md),
   [product features](game/features.md), [offline patches](game/offline.md).
5. [Game service integration](game-services.md): product opt-in and runtime limits.
   Signature resolution, service presence and in-game correctness are separate evidence.

## Agent / IDE user — Discover the SDK via MCP

1. [Agent and IDE integration](agent-integration.md): compiler/catalogue
   prerequisites, stdio launcher, client configuration and troubleshooting.
2. [SDK to MCP to plugin](sdk-mcp-tutorial.md): discover a contract, request a
   scaffold, validate manifest/widgets and run the native checker.
3. [MCP server authority](../tools/sco-core-mcp/README.md): six default tools, six opt-in
   discovery tools (including guide search with `search_guides` and `get_guide_section`)
   and three opt-in development tools, allowlist, provenance, limits and separate
   administrator-owned runner settings.
4. [Reference snapshots](reference-snapshots.md): bind discovery/examples to
   exact source/archive inputs; development previews are not approved releases.

MCP is static discovery by default; the opt-in development tools build and
check plugins on this machine only, never a host or the game.
Client-specific instructions live in the integration guide, not in this index.

## Maintainer — Build, contribute, release

1. [Contributing](../CONTRIBUTING.md) and [building/testing](building.md):
   source policy, named tests, CI and product adoption.
2. [Changelog](../CHANGELOG.md), [SDK 2.3.0 notes](releases/sdk-v2.3.0.md) (latest published),
   [SDK 2.2.0 notes](releases/sdk-v2.2.0.md), [SDK 2.1.0 notes](releases/sdk-v2.1.0.md),
   [SDK 2.0.0 notes](releases/sdk-v2.0.0.md) and
   [published releases](https://github.com/scubamount/sco-core/releases/latest):
   keep SDK/package, plugin ABI and game-pack version coordinates distinct.
3. [Release checklist](legal/release-checklist.md) and
   [release decisions](legal/release-decisions/README.md): candidate versus
   signed approval versus actual publication; releases are maintainer acts.
4. [Documentation site](../site/README.md): curated public inputs, generation,
   checks and existing Pages publication workflow.
5. [IP policy](../IP_POLICY.md), [third-party notices](../THIRD_PARTY_NOTICES.md)
   and [dependency inventory](legal/third-party-inventory.md).
6. [Design: opt-in runtime MCP](design/runtime-mcp.md): separate implemented
   default-off metadata launcher, historical decisions and outstanding
   Windows/manual acceptance.

### Historical designs and evidence

These are dated plans/audits, not substitutes for current contracts:
[framework plan](framework.md), [service safety](design/service-safety.md),
[service-table lifetime audit](design/service-table-lifetime.md),
[game services](design/game-services.md), [game-world spikes](design/game-world-spikes.md),
[multiplayer/bridges](design/multiplayer.md), [VFS/DataCore](design/vfs-datacore.md),
[release-hardening baseline](legal/release-hardening-baseline.md) and
[plugin-licensing options](legal/plugin-licensing-options.md).
