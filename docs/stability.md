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

Plugin ABI 1.x (`sco_api.h`, currently 1.1; its released layout is pinned by the ABI
freeze) only grows. New functions and fields go at the end of their struct,
`SCO_API_MINOR` goes up, and nothing released is removed, renamed, retyped or reordered.
The rules are in [Plugin API v1: Compatibility](api-v1.md#compatibility), and the
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
  version macro in its header. For example, `sco.widgets` 1.2
  (`SCO_WIDGETS_VERSION_1_2`) appended the node-tag members to the 1.1 table.
- Existing members are never removed, renamed, retyped or reordered, and the layout of a
  released table never changes.
- A caller checks that the table is non-null, that its `size` covers the whole member it
  is about to call, and that the member is non-null
  ([reading the rows](contracts.md#reading-the-rows)). A newer table answers an older
  minor; an older table is shorter, and the size check catches it.

A change that cannot be made at the tail needs a replacement table: a new service under
a distinct name (for example `game.loadout2`), with its own header and version macros,
published next to the unchanged old name. Plugins move by querying the new name. The
kernel publishes one table per name: `provide_service` refuses a name that is already
published, and `query_service` answers `SCO_UNAVAILABLE` when the major differs
([services](api-v1.md#services-11)). Two major versions therefore never
share a name. Publishing them under one name would need a kernel change with its own
design and ABI review; the current host does not support it.

## Stable and Experimental

**Stable**: the table is never removed or changed incompatibly within SDK 2.x. It only
grows at its tail as above. Once a released SDK has carried the Stable label for a table,
the table stays Stable for the rest of SDK 2.x, whatever its later evidence.

**Experimental**: the table grows the same way, but a later SDK 2.x minor release may
replace it with a new table under a distinct name, as above. When that happens, the old
name stays published and is marked deprecated in the [changelog](../CHANGELOG.md) and in
its documentation for at least one further SDK minor release. If SDK 2.N ships the
replacement, 2.N and 2.(N+1) both still publish the old name; 2.(N+2) is the earliest
release that may drop it.

Either way, stability is independent of availability and of evidence. A Stable table's
capability can be OFF by default, can answer `SCO_UNAVAILABLE` when the game is not
ready or a row did not resolve, and can have no in-game run at all. Evidence levels are
recorded separately, per capability, in [game status](game/status.md).

## How a game table gets its label

Game-pack tables come from the optional Star Citizen game pack and are declared in
`include/sc_*.h`: the `game.*` tables, plus `teleport.spatial`
([`sc_spatial.h`](../include/sc_spatial.h)) and `spawn.entities`
([`sc_spawn.h`](../include/sc_spawn.h)). Their engine side changes with the game. A new
label follows one rule, set by the maintainer:

- **Stable** if at least one of the table's capabilities has evidence level
  `sigcheck_resolved` (its rows resolve in a `sco-sigcheck` report of a game build),
  `off_pending_probe` (its rows resolve and one in-game question is open) or
  `in_game_verified` (a dated run in a game session; no capability has this level yet).
- **Experimental** otherwise: every capability is synthetic-only, blocked or
  undocumented.

The rule decides a label until an SDK release carries it. After that, Stable is a
ratchet: if a later game patch takes away every qualifying evidence level, the table
keeps its Stable label and its promise, and its capability answers unavailable until the
game pack is updated. An Experimental table becomes Stable as soon as one capability
qualifies.

The `game.*` event bus ([`sc_game_events.h`](../include/sc_game_events.h)) has no service
table but carries a label by the same rule. For the bus, Stable means that event names
are never removed or renamed and that payload structs grow only at their tail: every
payload starts with `uint32_t size`, which a subscriber checks before reading a field
past what it was built against.

The two signature-row groups in the status table (mining rows and CryPak adapter rows)
carry no label. They are not service tables: they are read by host-internal code and the
internal C++ API (version 0, below), and a game patch can stop them resolving.

The label is the `stability` field of each service and event-bus entry in
[`sdk/status_registry.json`](../sdk/status_registry.json), the same file that records
the evidence. A Stable entry also records `stable_since`, the SDK release that first
carries the label (`sdk-v2.4.0` for every Stable entry today).
[`tools/status_registry.py`](../tools/status_registry.py) refuses an entry without a
label, an unknown label, a label on a signature-row group, `stable` without
`stable_since`, and `experimental` when a capability qualifies. Until the `stable_since`
release is out (while `sdk/VERSION` still names it), it also refuses `stable` when no
capability qualifies. After that it refuses demoting the entry. It renders the label in
the Stability column of the [service table](game/status.md#service-tables-and-capabilities),
with `-` for a signature-row group.

The current-source MCP tool `get_service_status` exposes all static rows for a
service, including rows that do not map to a method. Its schema-2 catalogue records
the SHA-256 of the exact registry bytes. `source_binding.state == "matched"` means
those bytes match; schema-1 catalogues report `unbound`, and a schema-2 mismatch
makes status unavailable. This binds two static files and does not observe a running
provider or game.

The SDK source registry labels game-pack service tables Stable or Experimental and
the `game.*` event bus Stable. The Stability column of the status table is the
current list; check the selected kit's registry for its own snapshot.

## What this page does not cover

- The internal host C++ API in `include/sco/` ([`sco` namespace](api.md)) is version 0
  and outside this promise.
- The language layers: the C++ wrappers in `include/scosdk/` ([C++ SDK](sdk-cpp.md)), the
  C# `Sco.Sdk` bindings ([C# SDK](sdk-csharp.md)) and the Lua API
  ([Lua plugins](plugins.md#lua-plugins)). This page makes no promise about their
  source-level API. They call the C tables above, so the table's promise holds for what
  they call.
- Wire and shared-memory layouts with their own version constants: `sc_ipc.h`
  (`SC_IPC_VERSION`), `sc_titanlink.h` (`TL_LAYOUT_VERSION`), `sc_voxel_bridge.h`
  (`VX_LAYOUT_VERSION`) and the `sc_net.h` packet header (`SC_NET_PROTOCOL_VERSION`).
  Both sides must agree on that constant, and any layout change bumps it
  ([bridge layouts](ipc.md#bridge-layouts), [net wire](net-wire.md)).
- The `plugin.ini` manifest and its keys ([Plugins](plugins.md)).
- The ImGui draw context that `sco.ui` passes through. Using it is a third-party ABI
  coupling outside the frozen C ABI
  ([UI](ui.md#optional-third-party-abi-coupling-not-the-frozen-c-abi)).
- Game behavior. A game patch can stop a Stable table's rows from resolving. Its
  capability then answers unavailable until the game pack is updated. The table's name
  and layout stay as promised, and so does its label once a released SDK has carried it.
- In-game verification. A label is a compatibility promise, never a claim that anything
  was run in game.

## Reading labels from tools

The MCP discovery server ([agent integration](agent-integration.md)) returns `stability`
per service from `list_services` and per record from `get_capability_status`: `stable`,
`experimental`, or `unknown`. `unknown` means the server was started without a
repository root, its status source was refused or predates the labels, or the source
has no entry for that table. Host services and the plugin ABI are always `stable`.
Signature-row groups are not catalogue tables and do not appear.
