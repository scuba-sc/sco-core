# Roadmap and direction

sco-core is the SDK that Star Citizen offline mods are built on. This page says where the platform
is today, what stable SDK 2.4.0 delivers, and where the platform goes next. It is a direction, not a
release promise: there are no dates, and a feature named here exists only when its contract,
implementation and evidence say so in [game capability status](game/status.md) and the release notes.

SDK 2.4.0 is the current stable release, published on 2026-11-01 from source
commit `6106fa3639cbcc28f78a1ded8de2707de019f4d3`. The release includes five
profile archives, `SHA256SUMS`, SPDX records and release documentation. See the
[stable release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0).
RC1 and RC2 remain historical records in the [release notes](releases/sdk-v2.4.0.md).
The published bytes do not establish that a game capability has been verified in game.

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
| Out-of-game testing | `sco-plugin-check` runs a plugin against a rule-enforcing host without the game. The [scuba-engine](https://github.com/scuba-sc/scuba-engine) studio hosts plugins against mock game services. |

Consumer agents start with [roadmap and SDK planning](../sdk/skills/sco-plan-sdk-work/SKILL.md).
The workflow records this document's source/hash, goal, public contracts and observable
acceptance, then carries implementation through archive-only checks. The current-source discovery
profile exposes the complete fifteen-tool SDK/skill surface; constrained clients can select
`--minimal-tools`. [Defaults readiness](defaults-readiness.md) records experimental backend
prerequisites. Default availability is a goal; backend implementation and evidence remain
separate requirements.

### Start contributing today

For a first out-of-game contribution, follow this path:

1. Download `sco-sdk-2.4.0.zip` and `SHA256SUMS` from the [stable release](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0), verify the archive, and build the first plugin with [the getting-started guide](getting-started.md).
2. Use the bundled `sco-core-mcp` to discover a contract, generate a scaffold, and check the plugin with the [SDK and MCP tutorial](sdk-mcp-tutorial.md).
3. Exercise the plugin against `scuba-engine` mock services outside the game. Its native runtime targets Windows and Linux; the native editor is Windows-only, while Browser Studio and portable tools are separate. Mock results are not gameplay evidence.
4. When a community mod suggests a missing SDK capability, submit a clean-room gap report through a public SDK issue. Describe observed behavior and the requested contract without uploading game files, dumps, mod binaries or leaked source.

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

## 2. Milestone 1: SDK 2.4.0, the contributor platform

**Goal:** a developer can discover a capability, read its contract and limits, write a plugin in a
supported language, validate it without the game, and deploy it on a compatible host, without
reverse engineering anything.

Status: **shipped as stable** (`sdk-v2.4.0`, published 2026-11-01 from source
`6106fa3639cbcc28f78a1ded8de2707de019f4d3`; five profile archives with checksums,
SPDX records and docs). Plugin ABI 1.1 remains byte-identical. Star Citizen game
pack 0.8.0 targets verified build 4.10.197.2878. Publication is not gameplay
evidence: the new SDK capabilities have not been promoted to verified-in-game on
this page.

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

### Shipped and remaining acceptance

The stable release provides the published contributor platform described above. The remaining
acceptance work concerns consumer coverage and human gameplay evidence; it does not change the
release identity. Keep contract, implementation, availability and evidence separate. The product
and studio states below describe their current public source, not an in-game certification.

The [interactive one-page roadmap](https://scuba-sc.github.io/sco-core/roadmap/) is a companion
status view. This document is the maintained contributor roadmap; follow the release page and
public issue links below for current source evidence.

## 3. Milestone 2: consumers and out-of-game studio

SDK 2.4.0 is stable; consumer integration has advanced, while full suite and gameplay acceptance
remain separate gates.

- **sc-offline:** current `main` is `e0ed7dba102b3463b04e02af1ff49a2addabe7ea`. PRs #185 and #192
  are merged on the stable SDK baseline. [Issue #183](https://github.com/scuba-sc/sc-offline/issues/183)
  has synthetic VPN-range coverage; Windows firewall behavior and in-game join acceptance remain
  open.
- **scuba-engine:** current `main` is `7d9cf369cf718d6a108dd594288dc7d46267f166`. Native runtime
  builds target Windows and Linux; the native editor is Windows-only. Browser Studio and portable
  developer tools are separate. The Studio MCP bridge is implemented with 16 tools; its presence
  does not mean every acceptance scenario is certified. Full out-of-game suite certification is
  still a next-horizon task, and mock runs do not establish gameplay behavior.
- **sc-mp:** current `main` is `9b73627b07a4d7a585b68f38ba6a50e4fb3968fa`; integration PRs #14–#18
  and #20 are merged. Windows/Linux CI exercises a synthetic relay and host context. Authority and
  receipts are process-rooted and volatile; durable recovery, a native game authority bridge and
  in-game co-presence have not been demonstrated.
- **Core parity:** [issue #302](https://github.com/scuba-sc/sco-core/issues/302) remains open. The
  bounds-check paths are fixed, while valid-input parity across all signature rows is still pending.

## 4. Milestone 3: next horizon

Planned work is to close the remaining acceptance gaps in evidence order:

1. Owner-run, one-launch SDK 2.4.0 acceptance on the named game build, including the recorded V-row
   observations. No row becomes verified in game until its run evidence is recorded.
2. Durable sc-mp authority/recovery and a native ghost bridge, followed by normal in-game validation.
3. A complete out-of-game suite certification across the bundled plugins, mock providers, scenario
   runner and refusal paths.
4. Core valid-input parity for #302, then native expansion including container traversal,
   spatial/zone conversion, multi-hit raycasts, mission/economy catalogues, and seated actor/crew
   persistence.
5. Independent first-time-contributor dry run: follow the delivered archive-to-plugin
   documentation without help, then use observed friction to improve it.

Each capability still moves through contract, implementation, availability and evidence. Static
checks, mock results and CI are not gameplay evidence. Follow the linked public issues for current
implementation details; the human owner records and decides game acceptance.

## Out of scope, always

Offline only: no connection to Cloud Imperium Games' servers, no anti-cheat circumvention, no
telemetry and no accounts. See the [plugin rules](../sdk/docs/plugin-rules.md). sco-core is an
independent project, not affiliated with or endorsed by Cloud Imperium Games or Roberts Space
Industries.
