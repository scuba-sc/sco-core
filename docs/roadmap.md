# Roadmap and direction

sco-core is the SDK that Star Citizen offline mods are built on. This page says where the platform
is today, what SDK 2.4.0 is meant to deliver, and where it goes after that. It is a direction, not a
release promise: there are no dates, and a feature named here exists only when its contract,
implementation and evidence say so in [game capability status](game/status.md) and the release notes.

The newest published SDK is [2.3.0](releases/sdk-v2.3.0.md). SDK 2.4.0 is an unpublished
development candidate ([draft notes](releases/sdk-v2.4.0.md)).

## How to read status on this page

Every capability is tracked on four separate axes. A green build, a passing mock or a resolved
signature never stands in for the last one.

| Axis | Question it answers |
|---|---|
| Contract | Is there a versioned table in a public header, with documented rules? |
| Implementation | Does a backend exist: synthetic (tests and mocks), static-checked native, or none? |
| Availability | Is it on by default, off by default (opt-in), or unavailable? |
| Evidence | Synthetic tests, static signature check against the verified game build, or a recorded in-game run? |

**Fail-closed** means a service whose native backend is not grounded answers `SCO_UNAVAILABLE`. It
never installs a guessed hook. A plugin queries the table, checks the capability, and degrades
cleanly. "Not run in game" is written wherever it is true.

## 1. The platform today, and how to contribute

sco-core puts the game's internals behind one versioned plain-C interface, so a mod targets
sco-core instead of a game build, and a game patch is fixed once here instead of in every mod.

| Layer | What you use |
|---|---|
| Plugin ABI | [`sco_api.h`](../include/sco_api.h), version 1.1. It only grows: a plugin built today keeps loading on later 1.x hosts. |
| Languages | C, header-only [C++20](sdk-cpp.md), [C# (.NET 8, NativeAOT)](sdk-csharp.md), sandboxed [Lua](../sdk/docs/lua.md), and [data packs](../sdk/docs/data-packs.md) with no code. |
| Host services | Storage (SQLite), widgets and UI, settings, DataCore files and packs, local IPC, private-session messages. |
| Game pack | Optional `game.*` service tables for Star Citizen on the verified build. See [game services](game-services.md). |
| Agent and IDE discovery | A read-only stdio MCP server that ships with the SDK: search the API, read contracts and examples, check compatibility, validate a manifest, and generate a plugin scaffold. See [agent integration](agent-integration.md) and the [SDK and MCP tutorial](sdk-mcp-tutorial.md). |
| Out-of-game testing | `sco-plugin-check` runs a plugin against a rule-enforcing host without the game. The [scuba-engine](https://github.com/scubamount/scuba-engine) studio hosts plugins against mock game services. |

### Start contributing today

| You want to | Start here |
|---|---|
| Write your first plugin | [Start a plugin](getting-started.md), then [plugins](plugins.md) and the [plugin rules](../sdk/docs/plugin-rules.md) |
| Build and test the core | [Building and testing](building.md) and [CONTRIBUTING](../CONTRIBUTING.md) |
| Ask for a capability the SDK lacks | Open an issue with the SDK gap template; say what your plugin needs, not how the game does it |
| Add or fix a signature row | [Adding a signature](adding-signatures.md) and [sigcheck](sigcheck.md) |
| Improve docs, examples or tests | Any guide in [docs by audience](README.md); every example in the SDK must build and pass `sco-plugin-check` |
| Report what you saw in game | An issue with the game build, the plugin, what you did and what happened; this is how a capability earns in-game evidence |
| Use an AI agent | Point it at the bundled MCP server; it reads recorded contracts and never touches the game |

Contributions follow [IP_POLICY.md](../IP_POLICY.md): no game files, dumps or leaked code, and you
must have the right to contribute what you send.

## 2. Milestone 1: SDK 2.4.0, the contributor platform (current candidate)

**Goal:** a developer can discover a capability, read its contract and limits, write a plugin in a
supported language, validate it without the game, and deploy it on a compatible host, without
reverse engineering anything.

Status: **development candidate, not released.** Plugin ABI 1.1 stays byte-identical. Star Citizen
game pack 0.8.0 on verified build 4.10.197.2878.

### Developer kit

- Five checksummed archive profiles: `core`, `game-sc` (overlay), the combined kit, `core-source`
  and `host-source-sc`.
- The compiled API catalogue and the stdio MCP discovery server ship inside the combined kit, so an
  agent works from the archive with no source checkout.
- Plugin scaffolding and `sco-plugin-check`, exercised against the packaged archive rather than the
  source tree.

### Host and UI foundations

| Surface | What it adds |
|---|---|
| `sco.widgets` 1.2 | Retained panels with stable node tags that tools can read back. |
| `sco.storage` 1.1 | Bounded schema introspection: tables, views and columns. |
| `sco.diagnostics.publisher` 1.0 | Numeric and enum observations only, no free-form identifiers. |
| DataCore packs | Atomic stays the default; best-effort apply with per-record receipts; deep record cloning; `sco-dcb` lint and check. |
| `game.datacore_live` 1.0 | A host-backed seam for in-memory pool access; writes are a separate opt-in. |

### Game pack contracts

| Service | 2.4.0 contract | Native state in the candidate |
|---|---|---|
| `game.loadout` 1.2 | Asynchronous equip jobs with status, cancel and a nested report | Synthetic; off by default |
| `game.inventory` 1.1 and `game.inventory.contents` 1.0 | Copied port snapshot, item categories, bounded stored-item snapshots | Read held off; no native contents adapter |
| `game.actor_stats` 1.1 and `game.actor_stats_write` 1.0 | 29 raw condition reads; a narrow write allowlist behind default-off admission | Reads static-checked; writes held for an in-game probe |
| `game.input` 1.0 | Named input-action metadata by id or name | Metadata only; not input dispatch |
| `game.gamerules` 1.1 | Static parameter definitions (`list_params`) | Live read needs a backend: fail-closed |
| `game.entities` 1.2 | Copied class registry and lookup observations | Native lookup adapter: fail-closed |
| `game.world.control` 1.1 | Filtered rays with copied results; camera lease | Native filtered ray: fail-closed |
| `game.audio` 1.1 | Name resolution and a fire-and-forget `play_once` with its own capability | Native backend: fail-closed |

The four fail-closed rows are deliberate. Each has a narrowed native design (single-hit rays, exact
class-name lookup, value-change GameRules reads, fire-and-forget audio), but the evidence does not
yet show that the game objects involved stay valid for the whole call. Until it does, the table
answers unavailable and nothing is hooked. They are first in line after 2.4.0 (section 4).

### What still stands between the candidate and a release

- Build the final source, then pass the test, ABI, catalogue, legal and documentation gates and the
  hosted CI checks on that exact commit.
- Rebuild all five archives from that commit and record their hashes.
- Finish the provenance review of every shipped file.
- The maintainer approves, signs, tags and publishes. Nothing on this page is that approval.

## 3. Milestone 2: products and studio on the 2.4.0 SDK

SDK 2.4.0 is the platform; these releases prove it by being built only on it.

- **sc-offline (next release):** every built-in feature becomes a plugin that uses only the public
  SDK: spawning, loadouts, ASOP terminals, firearm and multitool attachments, contracts, NPCs, the
  inspector, mining, quantum travel, relocation, creative tools and build info. Community
  contributions land as plugins on the same interface.
- **scuba-engine studio:** run the whole product out of game. A mock provider for every `game.*`
  table lets plugin authors build, inspect and test against the same contracts the game pack serves.
- **sc-mp:** private co-presence between sc-offline players over LAN or VPN, built on the SDK's
  private-session and `sco.net` services, never on Cloud Imperium Games' servers.

## 4. Milestone 3: after 2.4.0, toward full engine coverage

The north star is that any game capability a mod needs has a published contract, a grounded native
backend, a mock, and in-game evidence. Work moves through the same pipeline every time:

1. **Contract:** a versioned table and its rules in a public header. The ABI only grows.
2. **Synthetic provider and mock:** tests in sco-core and a scuba-engine mock, so plugins can be
   written before the game side exists.
3. **Static check:** signature rows resolve on the verified build. Static checks find code; they do
   not prove behavior.
4. **Native backend, off by default:** only the grounded path, with fault containment, and fail-closed
   everywhere else.
5. **In-game evidence:** a recorded run on a named build.
6. **On by default:** only after the evidence.

Planned areas, in rough order:

| Area | Direction |
|---|---|
| Rays, entities, GameRules, audio | Ground and ship the four narrowed native backends that 2.4.0 holds fail-closed. |
| Spatial, zones and world control | Coordinate conversion across nested zones, filtered multi-hit rays, camera and aim control. |
| Containers, economy and missions | Native stored-container traversal, mission catalogue reads and writes, generated contracts. |
| Vehicles, crew and seated actors | Safe seated-actor removal, multi-crew state, persistent ASOP data. |
| Audio and events | Spatial audio events and typed entity and game event producers on the event bus. |
| Private co-presence | Session relay and ghost co-presence over LAN or VPN in sc-mp. |
| Patch days | Faster revalidation of every signature row on each new game build. |

Each row becomes contract and backend work items as it starts; watch this repository's issues and
pull requests to pick one up.

## Out of scope, always

Offline only: no connection to Cloud Imperium Games' servers, no anti-cheat circumvention, no
telemetry and no accounts. See the [plugin rules](../sdk/docs/plugin-rules.md). sco-core is an
independent project, not affiliated with or endorsed by Cloud Imperium Games or Roberts Space
Industries.
