# Roadmap and direction

sco-core is the SDK that Star Citizen offline mods are built on. This page says
what exists, what SDK <!--rv:sdk_version-->2.4.1<!--/rv--> delivers and what comes next. It has no dates and
promises nothing: a feature exists only when its contract, implementation and
evidence say so in [game capability status](game/status.md) and the release notes.

## Release status

<!-- release-block:current-release -->
SDK **2.4.1** uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../docs/game/builds.md)) and plugin ABI **1.1**. Download it from the
[`sdk-v2.4.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1) and verify each archive against that release's
`SHA256SUMS`; the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1/docs/releases/sdk-v2.4.1.md) list what changed. The previous release is
[SDK 2.4.0](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.0/docs/releases/sdk-v2.4.0.md).
<!-- /release-block:current-release -->

The release has five profile archives, `SHA256SUMS`, SPDX records and release
documentation. Its tests are synthetic. The milestone that shipped SDK 2.4.0 is
in [the 2.4.0 roadmap](roadmap-2.4.0.md).

## How to read status

Each capability is tracked on four separate axes. A passing build, a mock or a
resolved signature never stands in for the last one.

| Axis | Question it answers |
|---|---|
| Contract | Is there a versioned table in a public header, with documented rules? |
| Implementation | Does a backend exist: synthetic (tests and mocks), static-checked native, or none? |
| Availability | Is it on by default, off by default (opt-in), or unavailable? |
| Evidence | Synthetic tests, a static signature check against the checked game build, or a recorded in-game run? |

**Fail-closed** means a service whose native backend is not grounded answers
`SCO_UNAVAILABLE` and installs no guessed hook. A plugin queries the table,
checks the capability and degrades cleanly. Features with only synthetic or
static evidence are marked Not run in game.

## What exists

sco-core puts the game's internals behind one versioned plain-C interface, so a
mod targets sco-core instead of a game build, and a game patch is fixed once
here instead of in every mod.

| Layer | What you use |
|---|---|
| Plugin ABI | [`sco_api.h`](../include/sco_api.h), version 1.1. It only grows: a plugin built today loads on later 1.x hosts. |
| Languages | C, header-only [C++20](sdk-cpp.md), [C# (.NET 8, NativeAOT)](sdk-csharp.md), sandboxed [Lua](../sdk/docs/lua.md), [data packs](../sdk/docs/data-packs.md) with no code, and experimental [Rust bindings](../sdk/rust/README.md). |
| Host services | Storage (SQLite), widgets and UI, settings, DataCore files and packs, local IPC, private-session messages. |
| Game pack | Optional `game.*` service tables for Star Citizen on the checked build. See [game services](game-services.md). |
| Agent and IDE discovery | A read-only stdio MCP server in the combined kit: search the API, read contracts and examples, check compatibility, validate a manifest, generate a plugin scaffold. See [agent integration](agent-integration.md) and the [SDK and MCP tutorial](sdk-mcp-tutorial.md). |
| Out-of-game testing | `sco-plugin-check` runs a plugin against a rule-enforcing stand-in host. The [scuba-engine](https://github.com/scuba-sc/scuba-engine) studio hosts plugins against mock game services. Mock results are not gameplay evidence. |
| Launchers | A reference launcher in the core kit, with the [launcher contract](launcher-contract.md) and [building a launcher](building-a-launcher.md). |

Agents: start with the [planning skill](../sdk/skills/sco-plan-sdk-work/SKILL.md).
The MCP server's `--minimal-tools` option selects a six-tool profile for constrained clients.
[Defaults readiness](defaults-readiness.md) lists what each experimental backend
needs before it can default on.

## What 2.4.1 delivers

The full list is under `sdk-v2.4.1` in the [changelog](../CHANGELOG.md) and in the
[release notes](releases/sdk-v2.4.1.md).

- **Game host:** the opt-in `Composition::ProcessRetained` option, where the
  shell keeps code mapped until the process exits and `Stop` closes admission
  without unloading ([game host](game/host.md)). The strict default is unchanged.
- **App shutdown:** `app::Stop` refuses while a managed service pin is live, then
  closes managed acquisition, service publication and plugin loader entry until
  the next `app::Start` ([plugins](plugins.md#unloading)).
- **Contracts:** the separate `sco.storage.metadata` 2.0 interface
  ([storage metadata](storage-metadata.md)), `sco.widgets` 1.4 tagged events
  ([widgets](../sdk/docs/widgets.md)) and `game.loadout` 1.3 outfit slots
  ([loadout](game/loadout.md)). The outfit capabilities are off by default.
- **Host menu:** the host-side `sco.widgets.view` reader ([widgets view](widgets-view.md)).
- **Signatures and diagnostics:** a faster `FindPattern` and a per-pass index of
  RIP-relative `lea` instructions, with unchanged results, and startup diagnostics
  in the game pack logs.
- **Relocation and prefabs:** relocation completes at once when the player is
  already at the target. Prefab readiness follows the resolved rows.
- **Tools and kits:** an archive verifier and folder preflight, MCP tool titles
  and descriptions, a multi-word `search_api`, a renderer fix for unlabelled rows
  and NUMBER sliders, experimental Rust bindings with a sample plugin, and the
  Rust reference launcher.
- **Game pack 0.8.1** for build 4.10.200.58746. Its locators are static-checked.

Four services from 2.4.0 stay off by default in 2.4.1: `game.world.raycast_filtered`,
`game.entities.lookup`, `game.audio` and `game.gamerules.read`. Each has a narrowed
native design, but the evidence does not yet show that the game objects involved
stay valid for the whole call, so the table answers unavailable and nothing is
hooked. [Game capability status](game/status.md) lists each default.

## Consumers

These repositories move independently of this page. Recheck an exact ref before
you describe what a consumer has adopted.

- **sc-offline:** [Issue #183](https://github.com/scuba-sc/sc-offline/issues/183) has synthetic
  VPN-range coverage; Windows firewall behavior and in-game join acceptance are
  open.
- **scuba-engine:** the native runtime builds for Windows and Linux; the native
  editor is Windows-only. Browser Studio and the portable developer tools are
  separate. The Studio MCP bridge of the published scuba-engine has 16 tools.
- **sc-mp:** integration PRs #14 to #18 and #20 are merged. Windows and Linux CI
  run a synthetic relay and host context. Authority and receipts are
  process-rooted and volatile. Durable recovery, a native game authority bridge
  and in-game co-presence do not exist yet.
- **Core parity:** [issue #302](https://github.com/scuba-sc/sco-core/issues/302)
  is open for valid-input parity across all signature rows. The bounds-check
  work landed in [PR #304](https://github.com/scuba-sc/sco-core/pull/304), merge
  commit `7ee83fa95680aa482d8b160987b472547a67b888`.

## Next

Planned work, in order:

1. **Owner-run acceptance of SDK 2.4.1** in one game launch on build
   4.10.200.58746. No capability's evidence level changes until its run is
   recorded.
2. **Zone lookup beyond the player's chain.** Travel to another planet or moon by
   name needs one new engine row (child enumeration or a zone-by-name lookup)
   that Core does not have. Not implemented. See the [design note](design/zone-lookup.md).
3. **Plugin catalogue signing key.** The reference launcher's catalogue client
   checks a signed index against a trust root. The shipped trust root is empty
   because no production signing key exists, so a catalogue works from a local
   folder only and every entry lands disabled. Adding a key is a release-time act
   of the project owner, not a code change.
4. **A Rust host.** The reference launcher is Rust; the in-game host and menu stay
   C++. A Rust host and a Rust in-game menu on top of `sco.widgets.view` come
   later, after the menu has run in a game. Nothing in 2.4.1 depends on it.
5. **sc-mp:** durable authority and recovery, and a native ghost bridge.
6. **Out-of-game certification:** a complete suite across the bundled plugins,
   mock providers, scenario runner and refusal paths.
7. **Core valid-input parity** for #302, then native expansion: container
   traversal, spatial and zone conversion, multi-hit raycasts, mission and economy
   catalogues, and seated actor and crew persistence.
8. **A first-time-contributor dry run:** follow the archive-to-plugin docs without
   help, then fix the friction it shows.

Follow the linked issues; the project owner records and decides game acceptance.
The [interactive roadmap](https://scuba-sc.github.io/sco-core/roadmap/) is a
companion view of the same status.

## Contribute

| You want to | Start here |
|---|---|
| Write your first plugin | [Start a plugin](getting-started.md), then [plugins](plugins.md) and the [plugin rules](../sdk/docs/plugin-rules.md) |
| Discover a contract and scaffold a plugin | The bundled MCP server and the [SDK and MCP tutorial](sdk-mcp-tutorial.md) |
| Test without the game | scuba-engine mock services; mock results are not gameplay evidence |
| Build and test the core | [Building and testing](building.md) and [CONTRIBUTING](../CONTRIBUTING.md) |
| Ask for a capability the SDK lacks | Open an issue with the SDK gap template. Say what your plugin needs, not how the game does it. Do not upload game files, dumps, mod binaries or leaked source. |
| Add or fix a signature row | [Adding a signature](adding-signatures.md) and [sigcheck](sigcheck.md) |
| Improve docs, examples or tests | Any guide in [docs by audience](README.md). Every SDK example must build and pass `sco-plugin-check`. |
| Report what you saw in game | An issue with the game build, the plugin, what you did and what happened |
| Use an AI agent | Point it at the bundled MCP server; it reads recorded contracts and never touches the game |

Contributions follow [IP_POLICY.md](../IP_POLICY.md): no game files, dumps or
leaked code, and you must have the right to contribute what you send.

## Scope

Offline only: no connection to Cloud Imperium Games' servers, no anti-cheat
circumvention, no telemetry and no accounts. See the
[plugin rules](../sdk/docs/plugin-rules.md). sco-core is an independent project,
not affiliated with or endorsed by Cloud Imperium Games or Roberts Space Industries.
