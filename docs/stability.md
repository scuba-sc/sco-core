# Stability

What a plugin or product can rely on across SDK 2.x releases. Three questions are kept
apart on purpose:

- **Stability**: will code written against this table keep compiling and working in
  later SDK 2.x releases? That is this page.
- **Availability**: is the service published, and is its capability on, in a given host
  and game session? See [game services](game-services.md) and
  [game status](game/status.md).
- **Evidence**: how far has it been checked (signature rows, synthetic tests, a run in a
  game session)? See the evidence levels in [game status](game/status.md#how-to-read-it).

A Stable table is not a promise that its capability is on or that it works in the game.
Nothing on this page has been run in game.

## The plugin ABI

Plugin ABI 1.x (`sco_api.h`, frozen at 1.1) only grows. New functions and fields go at
the end of their struct, `SCO_API_MINOR` goes up, and nothing released is removed,
renamed, retyped or reordered. The rules are in
[Plugin API v1: Compatibility](api-v1.md#compatibility), and the
[ABI freeze](api-v1.md#abi-freeze) checks them in CI on every change.

## Host services

Every host service table under the reserved owner `sco`, declared in an
`include/sco_*.h` header, is **Stable**:

| Service | Version | Header |
|---|---|---|
| `sco.content` | 1.0 | [`sco_content.h`](../include/sco_content.h) |
| `sco.datacore` | 1.1 | [`sco_datacore.h`](../include/sco_datacore.h) |
| `sco.diagnostics.publisher` | 1.0 | [`sco_diagnostics_publisher.h`](../include/sco_diagnostics_publisher.h) |
| `sco.ipc` | 1.0 | [`sco_ipc.h`](../include/sco_ipc.h) |
| `sco.net` | 1.0 | [`sco_net.h`](../include/sco_net.h) |
| `sco.settings` | 1.0 | [`sco_settings.h`](../include/sco_settings.h) |
| `sco.storage` | 1.1 | [`sco_storage.h`](../include/sco_storage.h) |
| `sco.ui` | 1.0 | [`sco_ui.h`](../include/sco_ui.h) |
| `sco.ui.surfaces` | 1.0 | [`sco_ui_surfaces.h`](../include/sco_ui_surfaces.h) |
| `sco.widgets` | 1.2 | [`sco_widgets.h`](../include/sco_widgets.h) |

A host service is still optional: a host or game build that does not offer one does not
publish it, and `query_service` answers `SCO_NOT_FOUND`
([Host-owned services](api-v1.md#host-owned-services)).

## How a service table grows

Every published service table, Stable or Experimental, follows the same rule within its
major version:

- It grows only at its tail. New members go after the last one, behind a new minor
  version macro in its header (for example `SCO_WIDGETS_VERSION_1_2` added stable node
  tags to `sco.widgets` 1.1).
- Existing members are never removed, renamed, retyped or reordered, and the layout of a
  released table never changes.
- A caller checks that the table is non-null, that its `size` covers the whole member it
  is about to call, and that the member is non-null
  ([reading the rows](contracts.md#reading-the-rows)). A newer table answers an older
  minor; an older table is shorter, and the size check catches it.

A change that cannot be made at the tail needs a new major version: a new table with its
own version macro, published beside the old one.

## Stable and Experimental

**Stable**: the table is never removed or changed incompatibly within SDK 2.x. It only
grows at its tail as above.

**Experimental**: the table grows the same way, but a later SDK 2.x minor release may
replace it with a new major version. When that happens, the old version stays available
and is marked deprecated in the [changelog](../CHANGELOG.md) and in its documentation for
at least one further SDK minor release. If SDK 2.N ships the replacement, 2.N and 2.(N+1)
both still publish the old version; 2.(N+2) is the earliest release that may drop it.

Either way, stability is independent of availability and of evidence. A Stable table's
capability can be OFF by default, can answer `SCO_UNAVAILABLE` when the game is not
ready or a row did not resolve, and can have no in-game run at all. Evidence levels are
recorded separately, per capability, in [game status](game/status.md).

## How a game table gets its label

`game.*` tables come from the optional Star Citizen game pack, and their engine side
changes with the game. Their label follows one rule, set by the maintainer:

- **Stable** if at least one of the table's capabilities has evidence level
  `sigcheck_resolved` (its rows resolve in a `sco-sigcheck` report of a game build),
  `off_pending_probe` (its rows resolve and one in-game question is open) or
  `in_game_verified` (a dated run in a game session; no capability has this level yet).
- **Experimental** otherwise: every capability is synthetic-only, blocked or
  undocumented.

The label is the `stability` field of each entry in
[`sdk/status_registry.json`](../sdk/status_registry.json), the same file that records
the evidence. [`tools/status_registry.py`](../tools/status_registry.py) refuses an entry
without a label, an unknown label, `stable` when no capability meets the rule, and
`experimental` when one does. It renders the label in the Stability column of the
[service table](game/status.md#service-tables-and-capabilities). The registry labels every
entry this way, including the `game.*` event bus and the two signature-row groups, which
are not service tables.

At the SDK 2.4.0 candidate the registry has 16 Stable and 20 Experimental entries. The
Stability column of the status table is the current list.

## What this page does not cover

- The internal host C++ API in `include/sco/` ([`sco` namespace](api.md)) is version 0
  and outside this promise.
- Game behavior. A game patch can stop a Stable table's rows from resolving; its
  capability then answers unavailable until the game pack is updated. The table itself
  stays as promised.
- In-game verification. A label is a compatibility promise, never a claim that anything
  was run in game.

## Reading labels from tools

The MCP discovery server ([agent integration](agent-integration.md)) returns `stability`
per service from `list_services` and per record from `get_capability_status`: `stable`,
`experimental`, or `unknown` when it was started without a repository root, its status
source was refused or predates the labels, or the source has no entry for that table. Host services and the plugin ABI are always `stable`.
