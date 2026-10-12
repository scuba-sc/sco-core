# How sco-core works

## The problem it solves

sc-offline works by finding pieces of `StarCitizen.exe` in memory: a function to call, a global pointer to read, a few bytes to patch. Star Citizen is rebuilt every patch, so those addresses move. The mod finds them by searching for byte patterns that survive rebuilds.

Before sco-core, each feature file carried its own patterns and checks, and reported failures in its own words, somewhere in `mod.log`. After a patch there was no single answer to "what broke?", and the only way to find out was to start the game.

sco-core makes every address a named row in a table, resolves all rows in one place, and writes one report. The same tables run against an `.exe` on disk, so a new game build can be checked before anyone plays it.

## Rows

A row is a `sco::SigDef` ([`sco/signatures.h`](../include/sco/signatures.h)):

```cpp
struct SigDef {
    const char* id;              // "teleport.to_camera": <feature>.<thing>
    const char* pattern;         // unique .text pattern, or nullptr when `resolve` is set
    int         ripDisp;         // >0: the result is the RIP target of the operand at match+ripDisp
    int         ripSize;         //      in an instruction of ripSize bytes; 0 = the match itself
    SigResolver resolve;         // function for rows a pattern can't express
    const char* needs[4];        // ids this row reads; nullptr-terminated
};
```

A row finds its address one of two ways:

- **Pattern row.** `pattern` must match exactly once in `.text`. The address is the match, or, with `ripDisp`/`ripSize`, the target of a RIP-relative operand inside the match (for example the global behind `mov rax, [rip+X]`).
- **Resolver row.** `resolve` is a function that gets the image and returns a `SigResult`. Use it when a pattern isn't enough: starting from a string the function references, checking a dozen offsets inside a function, or deriving an address from another row.

`needs` lists rows this row reads. A resolver may only call `sco::Sig()` on ids in its own `needs`.

### Ids

Ids are `<feature>.<thing>` in lowercase with underscores: `teleport.to_camera`, `teleport.entity_system`, `system.quit`. The feature part matches the file under `src/game/` and the header under `include/sco/game/`. Ids are unique across all tables; registering a duplicate fails.

## Results

Each row ends up in one state (`sco::SigState`):

| State | Report word | Means |
|---|---|---|
| `Ok` | `OK` | Found; `Sig(id)` returns the address |
| `Missing` | `MISSING` | Pattern row: 0 matches |
| `Ambiguous` | `AMBIG` | Pattern row: more than one match, so none is trusted |
| `Failed` | `FAILED` | Resolver said no, with a reason (`layout changed at +0x2b0`), or the row itself is broken (unknown need, circular need, no pattern and no resolver) |
| `Blocked` | `BLOCKED` | A row it needs isn't OK; this row wasn't tried |
| `NotRun` | `NOT RUN` | `ResolveAll()` hasn't run since the row was registered |

`Sig(id)` returns an address only for `Ok` rows, and `nullptr` for every other state and for unknown ids. Features never get a half-found address.

## Resolving

`sco::ResolveAll(image)` clears every result, then resolves each row depth-first: a row's needs are resolved before the row itself, whatever order the tables list them in. If a need isn't OK, the row is `Blocked` and its resolver never runs, so resolvers don't have to check their needs for `nullptr`. A need on an unknown id, or a cycle, makes the row `Failed`.

Calling `ResolveAll()` again re-resolves everything from scratch and gives the same answer for the same image.

## The report

`sco::LogSignatureReport(false)` writes:

```
[core] signatures: 4/5 OK
[core] FAILED   teleport.to_camera (layout changed at +0x2b0)
```

The first line always appears. Then one line per row that isn't OK, in registration order. `LogSignatureReport(true)` also lists the OK rows. In the game this goes to `mod.log`; in `sco-sigcheck` it goes to the terminal.

## Startup

In sc-offline (`src/dllmain.cpp`), after the offline patches are applied:

```cpp
sco::SetLogSink(ForwardCoreLog);                         // sco-core lines go to mod.log
sco::game::RegisterGameSignatures();                     // all game tables, once
sco::ResolveAll(sco::ModuleImage());                     // the running StarCitizen.exe
if (ResolveTeleportApi()) { ... }                        // features read their rows
...
sco::LogSignatureReport(false);                          // one block in mod.log
```

Then, on the first main-thread tick, `sco::app::Start` runs the host kit (capabilities, the `sco_api` table, plugins with `plugins = on`, `game.ready`) with `image` left null, since the rows are already resolved; `sco::app::Tick` runs on every tick after. See [C++ API § sco/app.h](api.md#scoapph-the-host-kit).

**Plugin load order.** Inside `Start`, built-in plugins load first, in the product's list order. Discovered plugins (native and Lua; data packs are indexed after the loads) follow in folder order, except that a plugin loads after every plugin its `requires` names, either by id (`requires = b`) or through one of its services (`requires = b.svc`; a service name starts with its provider's id, so the id is enough to know the provider before anything runs). `Discover` resolves this once the whole folder list is known: names the host or the game pack already published (`sco.storage`, `game.vehicles`) and capabilities count as provided, a built-in's id counts too, a cycle is refused (`requires cycle: a -> b -> a`), and so is a name nobody provides (`requires service x.y: no plugin provides it`). `LoadOrder` gives the order (a stable topological sort: the lowest folder index that is free goes next) and `UnmetRequires` re-checks just before each load that every provider really is `Loaded`, so a provider that fails to load refuses its dependents instead of starting them without it. Unload runs in reverse load order. Details: [Plugins § Load order](plugins.md#load-order-and-requires).

`sco::ModuleImage()` (Windows only) reads the running executable's PE headers to find `.text` and `.rdata`. In `sco-sigcheck`, `sco::FileImage::Load()` builds the same `Image` from the file on disk by copying each section to its RVA, so every scanner and resolver sees the same bytes at the same offsets. No relocations are applied and no game code runs; addresses are only compared.

## The status channel

Features tell the player what happened through `sco::Status("Spawning %s...", name)`. It stores the message and writes `[status] Spawning ...` to the log. The menu shows the latest message with `sco::GetStatus()`. Features don't include menu headers, so feature code and UI code can change separately.

## Threading

| Call | Thread safety |
|---|---|
| `RegisterSignatures`, `ResolveAll` | Startup only, one thread. Not safe to run while other threads call `Sig()`. |
| `Sig`, `SigReady`, `SigLookup`, `SignatureResult` | Safe from any thread once `ResolveAll` has returned (read-only) |
| `Status`, `GetStatus` | Safe from any thread (mutex) |
| `SetLogSink`, `Log` | Safe from any thread (atomic sink); the sink itself must be thread-safe |
| `Post`, `Subscribe`, `Unsubscribe`, `RegisterCommand`, `ListCommands`, `Invoke` | Safe from any thread. Tasks, event callbacks and commands always run on the game thread |
| `GameThreadTick`, `DrainTasks`, `Dispatch`, `Release` | Game thread only (the thread that called `SetGameThread`); elsewhere they run nothing. `GameThreadTick` and `DrainTasks` also refuse to run inside a task, callback or command |

## The runtime

[`sco/runtime.h`](../include/sco/runtime.h) is how work reaches the game thread. The host calls `SetGameThread()` once from the game's main thread and `GameThreadTick(nowMs)` on every main-thread tick. Each tick first runs the tasks queued with `Post()`, in order, then dispatches the `tick` event.

- **Task queue.** A fixed ring of 256 tasks, the fast path: posting into it never allocates. When the ring is full, posts go to an overflow list under the same lock, and while the overflow holds anything new posts go there too, so tasks run in posting order across both; each drain moves what fits back into the ring. The total waiting is capped at 65,536 (`kMaxQueuedTasksHard`) so a runaway plugin can't eat memory: past it, or out of memory, `Post` refuses with `TooMany` and queues nothing. A drain runs only the tasks queued when it started; tasks posted while it runs (a task re-posting itself too) wait for the next tick. `Release` drops an owner's tasks from both stages without allocating.
- **Event bus.** Subscribers are keyed by owner, event name and callback. The subscriber list is replaced on every change and a dispatch walks the list it started with, so changes never invalidate the walk. A new subscriber is called from the next dispatch; a removed one is flagged and skipped at once, even by a dispatch already running. Since tasks never run during a dispatch, a task posted after `Unsubscribe` is the safe place to free `ctx`.
- **Commands.** Features register named actions (`spawn.ship`) with typed arguments. `Invoke()` checks the argument count and types and the command's capability before calling it. On the game thread it runs at once; from another thread it copies the name and arguments, queues a task, and reports the result through the `done` callback on the game thread. The registry copies each command's strings and arg defs into a slot. A slot never moves or is freed, and `Release(owner)` hands the owner's slots back for the next registration, so a `ListCommands()` pointer describes its command until its owner is released (the memory stays mapped after that but may describe a later command).
- **Owners.** Subscriptions, commands, tasks and queued `Invoke` calls carry an owner handle. `Release(owner)` removes them all at once and is final: later calls naming that owner are refused, so nothing it adds can outlive it. That is what unloading or disabling a plugin needs.
- **Services and faults.** `QueryService` hands the caller the provider's own raw table; the runtime does not mediate calls through it. The Windows guard attributes a fault to the first plugin DLL in the faulting thread's frames rather than necessarily to the callout's owner (built-ins share the product's module and keep owner blame). The loader first quarantines the owner: lookup, registration and host scheduling are withdrawn, but cached raw tables are neither revoked nor made safe. Without a handed-out table, resource hooks run and the plugin becomes `crashed`. With a handed-out table, it becomes terminal `faulted`: no unload export or resource-release hooks run, and its backing state and code remain retained, including through `UnloadAll`; consumers must stop using cached tables and restart the game. This differs from an administrative live-session unload/reload, which is refused before teardown for an exposed provider, and from terminal host shutdown, which closes runtime admission before unloading still-loaded plugins. Retaining a DLL mapping after ordinary shutdown teardown is not a guarantee that its table remains callable. Details: [Plugins: crash containment](plugins.md#crash-containment), [unloading](plugins.md#unloading), and [service safety design](design/service-safety.md); the opt-in provider-side guard is not built.

The runtime is C++ and internal (version 0). The plain-C [`sco_api.h`](../include/sco_api.h) is a thin layer over it: `Result` and `ArgType` share their numbers with `sco_result` and `sco_arg_type`, and `Arg` and `ArgDef` share their layout with `sco_arg` and `sco_arg_def` (checked at compile time). `Command` is not `sco_command`: the host table ([`sco/host.h`](../include/sco/host.h)) builds `sco_command` views of registered commands for `list_commands`.

**Hot reload.** `sco::app::Reload(id)` (also the host command `sco.reload`) reloads the target and every transitively loaded dependent, using the discovered `requires` edges. It preflights the entire group and refuses it before teardown if any member is non-reloadable, busy, missing requirements or has handed out a service table. It unloads dependents before providers and reloads providers before dependents, each under a new handle: natives from fresh DLL copies, Lua plugins by running their scripts again. Unrelated plugins stay loaded. Details and failure semantics: [Plugins: hot reload](plugins.md#hot-reload).

## Copied host diagnostics (runtime MCP step 1)

[`sco/diagnostics.h`](../include/sco/diagnostics.h) is an internal C++ observation
facility, not a plugin ABI or RPC. `sco::app::Start` begins a copy after startup;
`Tick` advances it on the game thread and begins at most one refresh per second.
`Stop` invalidates the store before `game.exit` and plugin teardown. The any-thread
`DiagnosticSnapshot()` accessor returns only a completed `shared_ptr<const Snapshot>`;
workers never traverse `app::Plugins()` or ask the game thread to refresh on demand.
No diagnostics channel, credential, MCP registration or product authorization is added.
The frozen `sco_api` is unchanged.

Construction advances at most four work units per call, checking a soft 1 ms budget
between units. One bounded registry copy and the final sort are individual units;
existing mutex contention cannot be promised to stay under 1 ms. An observation is
an interval copy, not simultaneous sampling of all registries: its timestamp is the
**earliest** source time, so a slow build remains honestly stale. Only a complete,
sorted generation becomes visible; two generations and 64 local continuation tokens
are retained. Tokens are lookup-only, tool/filter/generation-bound, invalidated by
session reset, and expire after 60 seconds. An expired generation returns
`snapshot_changed`, not page one. Inventories cap at 1024 and carry truncation/loss.

Service metadata is copied under the existing registry mutex without table reads or
`QueryService`. Withdrawal history is bounded and best-effort; observation allocation
failure never rejects a successful publication or withdrawal. Exposure means the
owner has handed out **any** table, not a counted consumer lease. Owner/category/size
remain unknown unless explicitly registered by trusted host code; even a registered
size is a metadata assertion, never a size-prefix probe. Signature state comes from
existing results, without resolver/scanner execution or addresses/patterns. Actual
load/resolution generations are unknown rather than substituting load order or
snapshot generation. Capability gates are copied, never tested by invoking a method.

The optional `GamePackMetadata` and `DiagnosticsPluginMetadata` source slots are
absent by default. They reserve space for [#141](https://github.com/scubamount/sco-core/issues/141),
not publishers, gameplay records, game types or new plugin-authored v1 JSON fields.
Capture is a separate keyed host-only facility, inactive until explicitly started;
step 1 installs no capture into a product or the ordinary logger. See
[log privacy](log-privacy.md) and the [phased design](design/runtime-mcp.md#phased-pr-plan-and-acceptance).
Synthetic acceptance is `test_runtime_diagnostics_snapshot`; timing under Windows,
protected audit/security permissions and gameplay remain unverified. **Not run in game.**

## Limits

| Limit | Value | What happens at the limit |
|---|---|---|
| Rows across all tables | 512 (`kMaxRows`) | `RegisterSignatures` logs `signature table full` and returns false |
| Needs per row | 4 (`kMaxSigNeeds`) | Compile error |
| Pattern length | 96 bytes (`kMaxPatternBytes`) | The pattern matches nothing (`MISSING`), never a shortened match |
| Log line | 511 characters | Truncated |
| Status message | 255 characters | Truncated |
| Queued tasks | 65,536 in all (`kMaxQueuedTasksHard`); the first 256 (`kMaxQueuedTasks`) in an allocation-free ring, the rest in an overflow | `Post` returns `TooMany` |
| Event subscriptions | 512 (`kMaxSubscriptions`) | `Subscribe` returns `TooMany` |
| Commands | 512 live (`kMaxCommands`); `Release(owner)` frees its owner's slots, so loading and unloading plugins any number of times never exhausts them | `RegisterCommand` returns `TooMany` |
| Plugin handles | 256 (`host::kMaxPlugins`) for the life of the process; every load counts, reloads too | `NewPlugin` returns null; a reload leaves the plugin unloaded |
| Command strings | name, title, capability 63; help 255; arg name 31; arg help 127 | `RegisterCommand` returns `BadArg` |
| Arguments per command | 16 (`kMaxCommandArgs`) | `RegisterCommand` / `Invoke` return `BadArg` |
| Command reply | 255 characters | Truncated |

## Layers

sco-core is five layers. Each depends only on the ones below it; the first two are the kernel build, and CI's `kernel-only` job fails if they reach into the game pack.

```
 consumers      sc-offline (bootstrap + built-in plugins), sco-host-sim, third-party plugins, sco-sigcheck / sco-dcb
 plugin SDK     sco_api.h 1.1 and the service headers (sco_*.h host, sc_*.h game and product);
                C / C++20 (scosdk) / C# (Sco.Sdk) / Lua (game services read-only) / data packs
 game pack      Star Citizen (SCO_GAME_SC): signature rows (src/game), game services under the owner "game",
                the CryPak adapter, DataCore and sco.datacore
 host services  sco.storage, sco.ui, sco.settings, sco.ipc, sco.net (owner "sco")
 kernel         runtime (tasks, events, commands, owners), host table, plugin loader and crash containment,
                host kit, hot reload, hook engine, vfs, zone tree, signature engine, sco-lua
```

- **Kernel:** game-agnostic. It knows nothing about Star Citizen; a host for another game links the kernel targets and passes its own signature tables.
- **Host services:** published by the host under the reserved id `sco` before any plugin loads and withdrawn after the last unloads ([API v1](api-v1.md#host-owned-services)). Game-agnostic, so they live in the kernel build. (`sco.datacore` is also published under `sco`, but it belongs to the game pack: it patches Star Citizen's DataCore, so it exists only with `SCO_GAME_SC`.)
- **Game pack:** everything that knows Star Citizen. Its services are plain `query_service` tables under the owner `game` ([game services](game-services.md)); a game patch is fixed here, once, in the rows ([adding a signature](adding-signatures.md)).
- **Plugin SDK:** the contract plugins build on: `sco_api.h` (stable since sdk-v1.1.0; version 1 only grows), the per-service headers, each pinned by an `abi_*.c` test, the language layers and the zip that ships them ([plugin SDK](../sdk/README.md)). A header names its layer by prefix: `sco_` is a host service, `sc_` a game-pack or product service or wire.
- **Consumers:** products and plugins. sc-offline links `sco_app` and `sco_pak` (which bring the game pack) and runs its features as built-in plugins on the same ABI as third-party ones.

## Kernel and game pack

sco-core builds as a game-agnostic **kernel** plus one **game pack**, Star Citizen's. The boundary is a build option that CI enforces, not a promise in a document.

- **Kernel:** `sco_api.h` and the runtime (`src/api`), the host table (`src/host`), plugins and the host kit (`src/plugins`, `src/app`), the host services `sco.storage`, `sco.ui`, `sco.settings`, `sco.ipc` and `sco.net`, the hook engine, the VFS engine, the zone tree (`src/engine`), the signature engine (`sco_scan.cpp`, `sco_signatures.cpp`, `sco_pe_file.cpp`) and sco-lua.
- **Star Citizen game pack** (`SCO_GAME_SC`, on by default): every row in `src/game/*_sigs.cpp` and `sco::game::RegisterGameSignatures` (`sco_game_sc`), the CryPak adapter (`sco_pak`), DataCore and `sco.datacore` (`sco_datacore`, `sco_datacore_service`), `sco-sigcheck` and `sco-dcb`. The SDK headers `sco_datacore.h`, `sc_spatial.h`, `sc_spawn.h`, `sc_actors.h`, `sc_vehicles.h`, `sc_world.h`, `sc_entities.h` and `sc_game_events.h` describe game-pack services; they stay in `include/` and in the SDK zip, so plugins built against sdk-v1.1.0 keep working.
- **The gate:** CI's `kernel-only` job (`cmake.yml`) configures with `-DSCO_GAME_SC=OFF`, checks that no game-pack object or symbol was built, and runs the tests. Kernel code that reaches into the game pack fails that job. Where the kernel can use a game-pack service when one is there (the host kit's `Platform::dataCore`, sco-lua's `sco.datacore` binding, `sco-host-sim`'s pack lint), the code is guarded by `SCO_KERNEL_ONLY` or finds the service at run time.
- **Game services:** the game pack publishes its services under the reserved owner `game` with `host::ProvideGameService` (the sibling of `ProvideHostService`), from `src/game/services/` (library `sco_game_services`, Windows only: game reads run under SEH). A product turns them on with `Platform::gameServices`; builds without them log `[app] game services not built` and go on. Every table has the same shape: size first, `self` first on every function that changes the game, caller buffers, `last_error`, one capability per system, and a pin in `tests/abi_game_<name>.c`. Versions and capabilities are in [game services](game-services.md); what is on and how far it has been checked is in [status](game/status.md); the design is [design/game-services.md](design/game-services.md) (implemented).
  - `teleport.spatial` 1.0 (`spatial.cpp`) and `spawn.entities` 1.2 (`spawn.cpp`), moved from sc-offline's built-ins with the same tables.
  - `game.actors` 1.1 (`actors.cpp`): your player, NPC spawn and despawn, and an actor's health; `state` is in the table but stays off until an in-game run.
  - `game.vehicles` 1.0 (`vehicles.cpp`): the ship you're aboard, its seats, seating your own actors and NPCs, Flight Ready; one capability per system (`game.vehicles.seats`, `.seat`, `.flight_ready`).
  - `game.world` 1.0 (`world.cpp`): ray casts (sc-offline's build-mode ground ray, moved onto the `build.*` rows) and the camera's position and rotation; no field of view.
  - The `game.*` bus events (`events.cpp`): hooks on the game's own event queues record into a fixed ring and never run plugin code; the host's tick drains the ring and dispatches through the kernel event bus on the game thread, and `game.zone.changed` is a tick-side poll of the `teleport.*` reads. A hook installs only when its capability's rows are OK; the rows are in `src/game/events_sigs.cpp`. `game.vehicle.boarded` / `exited` stay off (the hooks only log).
  - `game.creative` 1.0 (`creative.cpp`): the local player's god mode, fly (noclip) and its speed, and infinite ammo for carried weapons and the ship you're aboard, on existing rows; one capability each (`game.creative.god_mode`, `.fly`, `.ammo`, `.ship_ammo`). The magazine detour is installed on first use. The same release hook turns off whatever an ordinarily released plugin switched on; restart-required retained-provider faults skip it and retain the toggles.
  - `game.entities` 1.1 is the next bullet.
  - One release hook, `spawn.cpp`'s `OnRelease` (`sco::AddReleaseHook`), releases the per-plugin state of `spawn.entities`, `game.actors` and `game.entities`: ordinary unload (including reload) or non-retained `crashed` cleanup removes owned NPCs/entities and forgets spawn ownership. Restart-required retained-provider faults skip this hook and keep the backing resources; lookup/scheduling withdrawal alone does not remove them.
  - The same library holds internal helpers a product calls directly, such as `sco::game::missions::ScriptLibrary` (the mission script library lookup, `sco/game/missions.h`).
  - Its own version line: `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` (0.3.0), tags `game-sc-vX.Y`, verified builds in [game-pack.md](game-pack.md).
- **`game.entities` 1.1** (`entities.cpp`, `sc_entities.h`; [game services](game-services.md), [API v1](api-v1.md#gameentities-11-game-pack)): the general entity service, started last (after `game.vehicles`, `game.world` and the `game.*` events) by `services::Start` (all-or-nothing, undone in reverse) and stopped first. It reuses the game pack's spawner and mover (`spawn.cpp`: `SpawnEntityInZone`, `MoveEntity`, the ownership lists) and the removal `game.actors` uses for its NPCs (`actors.cpp`: `RemoveEntityLater`). **One release hook** (`spawn.cpp`'s `OnRelease`) releases all three services' per-plugin state: ordinary unload or non-retained `crashed` cleanup removes the NPCs and entities the plugin spawned and ends its watches; a restart-required retained-provider fault skips this hook, retaining those resources while host callbacks are suppressed; an entity it `keep`s (1.1) is taken off its ownership list and stays, so neither the hook nor the stop sweep removes it. Capabilities `game.entities.transform`, `.spawn`, `.class_of`, `.query_radius` and `.watch`. The last two are decided by their rows (`entities.*` in `src/game/entities_sigs.cpp`, `sco/game/entities.h`) **and** by two constants, `kQueryRadiusConfirmed` and `kWatchConfirmed`, that stay `false` until the maintainer's in-game run has confirmed what the spike could not (the walk's thread and flag, the spawn batch's vector layout, the hooks' thread): `watch` matches an entity's class against the plugin's type filter inside the hooks (any thread, under one lock, into a per-watch ring) and the game thread's tick calls the plugin, one `Post`ed task per watch as a callout of the watch's owner, so a plugin that faults in its callback is the one blamed and a plugin released mid-callback gets no further event. `SCO_ENTITIES_DIAG=1` installs the two streaming hooks log-only and runs the walk once, to produce that confirmation in one run.
- **A product** picks the pack's parts: sc-offline links `sco_app` and `sco_pak` (which bring `sco_game_sc`), registers the game tables itself before its patches, and starts the host kit with `Platform::dataCore`. A host for another game would link the kernel targets and pass its own tables as `Platform::registerSignatures`.

## Source map

| File | What |
|---|---|
| `src/sco_scan.cpp` | `FindPattern`, `FindCString`, `FindRipLea`, `BytesMatch`, `Rel32` |
| `src/sco_signatures.cpp` | Registry, resolver, report |
| `src/sco_log_status.cpp` | Log sink and status message |
| `src/sco_image_win.cpp` | `ModuleImage()` for the running game (Windows) |
| `src/sco_pe_file.cpp` | `FileImage::Load()` for host tools |
| `src/api/sco_tasks.cpp` | Game-thread identity, task queue, `GameThreadTick`, `Release` |
| `src/api/sco_events.cpp` | Event bus |
| `src/api/sco_commands.cpp` | Command registry and `Invoke` |
| `src/api/sco_services.cpp` | Service registry (`ProvideService`, `QueryService`) |
| `src/hook/sco_hook.cpp` | Near caves, `WriteCode`, the detour registry (`sco/hook.h`) |
| `include/sco/engine/types.h` | `Vector3d`, `Quatd`, `Transform`: header-only 64-bit spatial math, XYZW helpers for the game's quaternion order |
| `src/engine/zone.cpp` | `ZoneTree` (`sco/engine/zone.h`): zones the host feeds, transforms between them |
| `src/api/sco_caps.cpp` | Capability registry |
| `src/host/sco_host.cpp` | The host's `sco_api` table and per-plugin handles |
| `src/settings/settings.cpp` | `sco.settings` (`sco/settings.h`): typed `[settings]` values, kept in `sco.storage`, the read-only table, `settings.changed`, the pages the product draws |
| `src/plugins/settings_ini.cpp` | The `[settings]` grammar of `plugin.ini` and reading, checking and writing one value (`sco/plugins.h`) |
| `src/api/internal.h` | Callout depth and the per-module halves of `Release`, shared by `src/api/` |
| `src/game/signatures.cpp` | `RegisterGameSignatures()`: the list of game tables |
| `src/game/services/*.cpp` | The game services and events (`spatial`, `spawn`, `actors`, `vehicles`, `world`, `entities`, `events`), the shared game reads (`reads.cpp`) and the mission script library lookup (`missions.cpp`); library `sco_game_services` |
| `src/ipc/`, `src/net/` | `sco.ipc` and `sco.net` ([IPC](ipc.md), [multiplayer messages](net.md)) |
| `src/game/<feature>_sigs.cpp` | One feature's rows and its typed accessor |
| `tests/test_core.cpp` | Unit tests against a synthetic image |
| `tests/test_runtime.cpp` | Runtime tests (run under ASan+UBSan and ThreadSanitizer) |
| `tests/test_host.cpp` | Capability and `sco_api` table tests (ASan+UBSan and ThreadSanitizer) |
| `tests/test_hook.cpp` | Detours over small functions written into executable memory (x86-64) |
| `tests/test_spatial.cpp` | Spatial math and the zone tree (ASan+UBSan and ThreadSanitizer) |
| `tools/sco-sigcheck.cpp` | The offline checker |
| `src/datacore/datacore.cpp` | `sco::datacore::Schema::Parse` (`sco/datacore.h`): the DataCore layout and its validation |
| `tests/dcb_builder.h` | Synthetic DataCore files for tests (32-, 36- or 40-byte records), written independently of the parser |
| `tests/test_datacore.cpp` | Parser round trips, each refusal, truncated and corrupted files (ASan+UBSan) |
| `tools/sco-dcb.cpp` | `sco-dcb info` and `records` over a `Game2.dcb` on disk |
