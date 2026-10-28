# Designing modular plugins

How to cut a feature into plugins that stay small, replaceable and safe to combine. It
applies to SDK 2.4.0 and plugin ABI 1.1, including the RC2 candidate source; every API name below is
in [`sco_api.h`](../include/sco_api.h) or a host-service header, and the rules are the
ones the host already enforces ([API v1](api-v1.md), [plugin.ini](../sdk/docs/plugin-ini.md),
[plugin rules](../sdk/docs/plugin-rules.md)). This page adds the design reasoning and a
worked pair of plugins, [`counter_service`](../sdk/examples/counter_service) and
[`counter_panel`](../sdk/examples/counter_panel).

## Contents

- [The principles in one list](#the-principles-in-one-list)
- [One feature per plugin](#one-feature-per-plugin)
- [Share through a versioned service table](#share-through-a-versioned-service-table)
- [Use another plugin's service](#use-another-plugins-service)
- [Grow and version your own table](#grow-and-version-your-own-table)
- [Declare dependencies with `requires`](#declare-dependencies-with-requires)
- [Capabilities and missing services are normal results](#capabilities-and-missing-services-are-normal-results)
- [Threads, events and callbacks](#threads-events-and-callbacks)
- [Clean unload](#clean-unload)
- [Public SDK only](#public-sdk-only)
- [Content-only changes: data packs](#content-only-changes-data-packs)
- [Settings and storage](#settings-and-storage)
- [Naming](#naming)
- [Splitting a feature into plugins](#splitting-a-feature-into-plugins)
- [Testing](#testing)
- [Anti-patterns](#anti-patterns)
- [The example pair](#the-example-pair)

## The principles in one list

1. One plugin owns one feature and its state.
2. Plugins share state and behaviour only through a service table published with
   `provide_service` under `<plugin id>.<name>`, or through commands.
3. A table starts with `uint32_t size`, grows only at its end, and carries a
   `(major << 16) | minor` version. Consumers check the size covers each member.
4. A hard dependency goes in `plugin.ini` `requires`; the host orders loads and
   refuses cycles and missing providers before any code runs.
5. `SCO_UNAVAILABLE` and `SCO_NOT_FOUND` are answers, not errors. Check `has()` and
   `query_service` results and say why a feature is off.
6. Callbacks are short and run on the game thread; work you start, you stop.
7. Everything you registered is withdrawn at unload; release what you own yourself.
8. Only public SDK headers and services. No raw game memory.
9. Content without code is a data pack.

## One feature per plugin

A plugin is the unit that the player enables, disables (an empty `disabled` file in
its folder) and updates, and the unit the host blames for a fault. Keep it to one
feature with one owner for its state:

- The plugin that owns a piece of state is the only one that writes it. Others ask the
  owner through its service or its commands.
- Commands are the human-facing surface (`<id>.<action>`, shown in the product's menu,
  callable with `invoke`); a service table is the plugin-facing surface. A feature
  usually offers both: `counter_service` registers `counter_service.value` for people
  and publishes `counter_service.counter` for plugins.
- If two parts of a plugin could sensibly be switched off independently, or one part
  is useful to other plugins without the rest, they are two plugins.

## Share through a versioned service table

A service is a plain-C table of function pointers one plugin publishes for others to
call directly ([API v1: Services](api-v1.md#services-11)).

```c
typedef struct counter_service_v1 {
    uint32_t size;  /* sizeof(counter_service_v1) as the provider built it */
    uint32_t _pad0;
    sco_result (*add)(int64_t delta, int64_t* out_value);
    sco_result (*get)(int64_t* out_value);
} counter_service_v1;

static const counter_service_v1 table = { sizeof(counter_service_v1), 0, counter_add, counter_get };

if (api->size >= offsetof(sco_api, provide_service) + sizeof(api->provide_service) && api->provide_service)
    r = api->provide_service(self, "counter_service.counter", 0x00010000, &table);
```

- **Name:** `<plugin id>` or `<plugin id>.<name>`, `[a-z0-9_.]`, 1-63 characters.
  `provide_service` refuses a name outside your id or one already published
  (`SCO_BAD_ARG`). Because the prefix names the provider, consumers can depend on it
  in `plugin.ini` before any code runs.
- **Put the contract in a header** that consumers copy: the table type, the name, the
  version constant, and for each member its thread, its arguments, its results and who
  owns any buffer. [`counter_service.h`](../sdk/examples/counter_service/counter_service.h)
  is a complete small example; the [contract catalogue](contracts.md) shows the same
  fields for the host's own tables.
- **Plain C across the boundary:** no C++ types, exceptions or varargs, fixed-width
  integers, explicit padding, caller-owned buffers with a size handshake
  (`SCO_TOO_MANY` and the size needed), as `sco_api.h` does.
- **Ids, never pointers into the game.** A table that deals with game objects takes
  and returns opaque `uint64_t` ids and resolves them on every call
  ([why](api-v1.md#services-11)).
- **Validate arguments in the provider.** A fault inside your table is blamed on you,
  including one caused by a bad pointer a caller passed in.
- **Keep the table and everything behind it alive.** Publish a `static const` table.
  Once any plugin has looked it up, the host refuses to unload or reload your plugin
  for the rest of the session (`sco.reload` answers `provides a service in use; restart
  the game to reload it`), so a new build of a provider needs a game restart
  ([hot reload](plugins.md#hot-reload)).
- **Bytes instead of a table:** for a call whose input and output are a byte buffer
  (a list, a transform), `register_raw` / `invoke_raw` give the same naming with a
  bytes-in, bytes-out handler on the game thread ([raw handlers](api-v1.md#raw-handlers-11)).

Language layers: C++ `sco::sdk::Provide`, `ServiceRef<T>`, `HasMember` and
`ServiceVersion` ([C++ guide](sdk-cpp.md)); C# `Provide`, `Query` and
`ServiceRef.Covers` ([C# guide](sdk-csharp.md)). Lua plugins can use host services
but cannot publish native service tables ([Lua guide](../sdk/docs/lua.md)).

## Use another plugin's service

```c
const void* found = NULL;
const counter_service_v1* c = NULL;
int64_t now = 0;
if (api->size >= offsetof(sco_api, query_service) + sizeof(api->query_service) && api->query_service &&
    api->query_service("counter_service.counter", 0x00010000, &found) == SCO_OK)
    c = (const counter_service_v1*)found;
if (c && c->size >= offsetof(counter_service_v1, add) + sizeof(c->add) && c->add)
    c->add(1, &now);
```

- **Query when you need it.** `query_service` works from any thread; a table you keep
  is a borrowed pointer, not a lease. The first successful lookup hands the provider's
  table out, and from then on the host keeps that provider loaded for the session
  (`sco.reload` refuses it), so look it up in the command or callback that uses it,
  not in `sco_plugin_load` "just to check": `counter_panel` queries only when
  `bump` or `show` runs. Never call a peer's table from
  your `sco_plugin_unload`, during shutdown or after its provider faulted
  ([native trust](../sdk/docs/plugin-rules.md#native-trust-and-supported-usage)).
- **Check every member before the call:** the table is not NULL, its `size` covers
  the whole member (`offsetof + sizeof`) and the member is not NULL. Covering one
  member says nothing about a later one. `COUNTER_SERVICE_HAS(table, member)` wraps it.
- **Read the result:** `SCO_OK` (same major, at least as new), `SCO_UNAVAILABLE`
  (another major, or older than you asked), `SCO_NOT_FOUND` (nobody publishes it).
  `out_vtable` is NULL unless `SCO_OK`.
- **Follow the provider's thread rules.** Any-thread lookup does not make every member
  any-thread.

## Grow and version your own table

- **Append only within a major.** A new member goes after the last one, `size` grows
  with it, and the minor goes up: 1.0 to 1.1. Nothing is removed, renamed, retyped or
  reordered. A 1.0 consumer keeps working against a 1.1 provider, and a 1.1 consumer
  sees from `size` that a 1.0 provider lacks the new member.
- **Ask for the oldest version you need.** A consumer that only calls 1.0 members asks
  for `0x00010000`; asking for 1.1 shuts out 1.0 providers for no reason.
- **A break is a new major.** A name holds one table and one version at a time, and a
  consumer asking for 1.x gets `SCO_UNAVAILABLE` from a 2.0 table. To keep old
  consumers working through a break, publish the new major under a second name (for
  example `counter_service.counter2`) beside the 1.x table.
- **Pin your layout** the way sco-core pins its own tables (`tests/abi_*.c`): static
  asserts on `sizeof` and each `offsetof`, so an accidental change fails the build.
  `counter_service.h` pins `counter_service_v1` that way, in C and in C++.
- The plugin's own `version` in `plugin.ini` is for people; the service version is the
  contract. Change them independently.

## Declare dependencies with `requires`

```ini
requires = counter_service.counter
```

`requires` lists up to 16 capabilities, services or plugin ids the plugin cannot run
without ([plugin.ini](../sdk/docs/plugin-ini.md#load-order), [loader](plugins.md#load-order-and-requires)):

- **Ordering:** a plugin loads after every plugin its `requires` names, by id or by one
  of its services, and unloads before them. `counter_panel` sorts before
  `counter_service` but loads after it.
- **Refusal instead of a broken start:** a cycle (`requires cycle: a -> b -> a`), a
  dotted name nobody can provide (`requires service x.y: no plugin provides it`), or a
  provider that is refused, disabled or fails to load refuses the dependent with the
  reason in `mod.log`. Nothing waits or hangs.
- **There is no `provides` key.** The order guarantees the provider loaded first, not
  that it published the name; check `query_service`'s result anyway.
- **Optional dependencies stay out of `requires`.** Then nothing orders the provider
  before you, so look the service up when you use it (in a command or callback), not
  only in `sco_plugin_load`, and offer the feature only while it is there.
- Host and game services (`sco.storage`, `sco.ui`, `game.vehicles`) and capabilities
  are published before plugins load, when the host and game build offer them, and need
  no ordering; name them in `requires` only if the plugin is useless without them (it
  is then refused where they are missing).

## Capabilities and missing services are normal results

The same plugin runs on hosts with different services and game builds with different
capabilities. Design for absence:

- `has("teleport")` answers whether a capability is ready on this game build. Check
  it before offering a feature and say why it is off (`hello` logs it on `game.ready`).
- A command can name its capability in `sco_command.capability`; the host then answers
  `SCO_UNAVAILABLE` for it without calling your code.
- When a service is `SCO_NOT_FOUND` or `SCO_UNAVAILABLE`, reply with the reason and
  return `SCO_UNAVAILABLE` (as `counter_panel.bump` does), or grey the feature out.
  Don't treat it as a failure, don't retry in a loop, and never call a NULL member.
- Every `sco_api` function added in 1.1 (`provide_service` through `register_raw`) is
  behind an `api->size` check. `sco-plugin-check --short-tables` serves a 1.0
  table and fails any call to a 1.1 member made without that check, and any plugin whose
  `sco_plugin_load` returns an error. Degrade under it instead of declining: a consumer
  answers `unavailable` and names the host as the reason (`counter_panel` replies `this
  host has no query_service (sco_api 1.0)`); a provider logs why, publishes nothing and
  keeps what still works (`counter_service` keeps its `counter_service.value` command).
  Both then pass the two checker runs that a `sco-new-plugin.py` project's `check`
  target makes. `api = 1.1` in `plugin.ini` still keeps a real 1.0 host from loading
  either plugin at all.

## Threads, events and callbacks

- Commands, `tick` callbacks, `run_on_game_thread` tasks and `invoke` completions run
  on the game's main thread ([threading](api-v1.md#threading)).
  `tick` arrives about 10 times a second: slow work there stalls the game.
- `run_on_game_thread`, `subscribe`, `unsubscribe`, `status`, `log`,
  `register_command`, `invoke`, `provide_service` and `query_service` work from any
  thread. A worker thread you start hands results to the game thread with
  `run_on_game_thread`.
- React to events instead of polling: `game.ready`, `tick`, `game.exit` (best effort:
  never rely on it to save data), `settings.changed`, and the events of the services
  you use (`net.state`, `net.peer`, the game pack's `game.*` events). `sco_api` has no
  event one plugin raises for another, so between plugins work on demand: call the
  provider when you need a value. If a consumer that runs every tick anyway must notice
  changes, a generation number in the provider's table is cheaper to compare than
  re-reading all of its state.
- Free a subscription's `ctx` only when no call can still be running
  ([freeing ctx](api-v1.md#freeing-ctx)).

## Clean unload

On ordinary unload, ordinary failed-load cleanup and non-retained crash cleanup, the host
withdraws everything the plugin registered with it: commands, subscriptions, queued tasks
and invokes, raw handlers, services, and what it registered with host services (UI tabs,
hotkeys, widget panels, settings). A provider whose table was handed out and then faults
is different: it becomes `faulted`, keeps its state and needs a game restart
([crash containment](plugins.md#crash-containment)). What the host cannot see is yours
to release:

- Stop and join any thread you started before your state is destroyed; the host does
  not track unmanaged threads.
- Free memory you allocated, close handles you opened, and `release_service` a service
  you withdraw early.
- Don't call other plugins' tables from `sco_plugin_unload`: dependents unload first,
  but a peer's table is not yours to use during teardown.
- Keep nothing important only in memory. A reload or restart loses it; persist through
  `sco.storage`.

## Public SDK only

Use the root ABI (`sco_api.h`), the public service headers (`sco_storage.h`,
`sco_settings.h`, `sco_widgets.h`, and the game-pack headers in the game extension), the
`scosdk/` C++ wrappers, the C# layer or Lua's `sco` table. Don't read or patch game
memory, hook game functions or include the host's internal `include/sco/` headers: a
plugin that needs a game feature uses the game-pack service for it, or asks for one.
The full list of what a plugin may and may not do is [plugin rules](../sdk/docs/plugin-rules.md),
and it is offline only.

## Content-only changes: data packs

A plugin whose change is content (missions, rules, scripts, lists, DataCore overrides
in `datacore/*.toml`) is a data pack: `kind = data`, no `entry`, no code
([data packs](../sdk/docs/data-packs.md)). It needs no compiler, runs no code, and can be
required by other plugins like any plugin id. Keep content in a pack and the code that
uses it in a separate native or Lua plugin.

## Settings and storage

- **Settings** the player changes go in a `[settings]` section of `plugin.ini`
  (`speed = int default 5 min 1 max 10 label "Walk speed"`). Read them through
  `sco.settings` (`get_int(self, "speed", &v)` and friends) and subscribe to
  `settings.changed`; the host validates and keeps the values ([settings](../sdk/docs/plugin-ini.md#settings)).
- **Storage** is per plugin: `sco.storage` gives each plugin its own SQLite database,
  key-value and SQL, that no other plugin can open ([storage](storage.md)). Share data
  with other plugins through your service, not through files.

## Naming

- The plugin id: 1-31 of `a-z 0-9 _`, equal to the folder name and, for a native
  plugin, to `sco_plugin_info.name`. `sco`, `host`, `menu` and `game` are reserved.
- Commands `<id>.<action>` (`counter_panel.bump`), services `<id>` or `<id>.<name>`
  (`counter_service.counter`), raw handlers `<id>.<name>`, network channels
  `<id>.<name>` (you pass the full name). For an IPC channel you pass only `<name>`
  (1-31 of `a-z 0-9 _`) and the host adds your id (`sco_ipc.h`). The host prefixes log
  and status lines with the id.
- Name the table type after the service and its major (`counter_service_v1`) and the
  version constants after the minor (`COUNTER_SERVICE_VERSION_1_0`).

## Splitting a feature into plugins

Take a waypoint feature: saved locations, a menu to manage them, and travel to one.

1. **Find the state and its owner.** The list of waypoints is state: one plugin,
   `waypoints`, owns it, stores it in its own `sco.storage` database and publishes
   `waypoints.list` 1.0 (add, remove, count, copy one by index into a caller buffer).
2. **Find the faces.** A menu tab (`sco.ui` or `sco.widgets`) is a consumer:
   `waypoints_panel`, `requires = waypoints.list`.
3. **Find the game-dependent part.** Travel depends on a game capability; a third
   plugin, or the panel itself, uses the game-pack service for it, gated on `has()`,
   so the list still works where travel is unavailable.
4. **Find the content.** A default set of locations is a data pack the owner reads.
5. **Check the arrows.** Dependencies point from faces and extras to the owner, never
   back, and never in a circle.

## Testing

- **`sco-plugin-check <folder>`** (in the SDK's `bin/`) reads `plugin.ini` with the
  host's rules, loads a native plugin against a stand-in host, fires `game.ready` and a
  few ticks, runs every command without arguments (`--invoke NAME ARG...` for others)
  and unloads it. `--cap NAME` makes `has(NAME)` answer 1 (for a capability, or a host
  or game service your `requires` names); `--short-tables` serves a 1.0 `sco_api`. It
  checks one folder at a time and does not connect one plugin's service to another, so
  a consumer checked alone sees its provider as missing: a good test of its degraded
  path. Run it both ways, as is and with `--short-tables` (the `check` target of a
  `sdk/tools/sco-new-plugin.py` project does). Both example plugins pass both runs, and
  the SDK's archive test asserts their replies.
- **Several plugins together:** sco-core's host simulator, `sco-host-sim` (in the
  repository and the core source kit), loads a whole plugin folder through the real
  neutral host, with `requires` ordering, and can invoke one command
  ([building and testing](building.md)). The CTest case `host_sim_examples` runs it
  over the example pair.
- **The scuba-engine studio**, a separate public project, loads a built DLL in its
  synthetic host to invoke commands and exercise widgets
  ([developer workflow](sdk-developer-mvp.md)).
- None of these is the game. A checker, simulator or studio pass is not an in-game
  result; say "not run in game" until it has been.

## Anti-patterns

- **God plugin:** one plugin with many unrelated features. It can't be disabled in
  part, one fault takes everything with it, and nobody else can reuse its pieces.
- **Private cross-plugin pointers:** handing another plugin a pointer to your globals,
  a shared struct outside a versioned table, or an exported DLL symbol. There is no
  size, version or lifetime contract, and the host can't order or withdraw it.
- **Shared files as an interface:** two plugins reading and writing one file. Use a
  service; each plugin's storage is its own.
- **Polling instead of events:** re-reading a setting, a capability or a peer's whole
  state every tick when `settings.changed`, `game.ready` or a generation number says
  when something changed.
- **Assuming a service exists:** calling through a table without checking
  `query_service`'s result, the table's `size` and the member, or calling a 1.1 member
  of `sco_api` without the `api->size` check.
- **Keeping a table forever:** caching a peer's table across a reload or a fault, or
  using it during shutdown.
- **Breaking a table in place:** reordering or retyping members under the same major.
- **Raw game memory:** offsets, patches or hooks in a plugin instead of a public
  service.

## The example pair

[`counter_service`](../sdk/examples/counter_service) owns a counter and publishes
`counter_service.counter` 1.0 (`add`, `get`) with a size-first table and an `api->size`
check before `provide_service` (on a 1.0 `sco_api` it publishes nothing and keeps its
command); it also registers the command `counter_service.value`, and its header pins the
table layout with static asserts.
[`counter_panel`](../sdk/examples/counter_panel) owns nothing: it says
`requires = counter_service.counter`, looks the table up only when a command runs (never
at load, so the provider stays reloadable until the feature is used), checks each
member's extent and answers `SCO_UNAVAILABLE` with a reason when the service is missing.
It shows a required dependency that still degrades: `requires` orders the load, but a
stand-in host, or a provider that published nothing, can leave the service absent.
Both are plain C against the neutral headers only. Both ship in the core SDK, are built
and checked one folder at a time by the SDK's archive test, and are loaded together by
CTest `host_sim_examples`, where `counter_panel.bump` answers `counter is 1` through the
provider's table. Not run in game.
