# How sco-core works

sco-core is a kernel that loads plugins and runs their work on the game thread, plus a set of signature tables that find the addresses a Star Citizen mod needs. This page explains the signature engine, the runtime, the layers and the source layout. Plugin authors can start at [Getting started](getting-started.md).

## The problem it solves

sc-offline works by finding pieces of `StarCitizen.exe` in memory: a function to call, a global pointer to read, a few bytes to patch. Star Citizen is rebuilt every patch, so those addresses move. The mod finds them by searching for byte patterns that survive rebuilds.

Before sco-core, each feature file carried its own patterns and checks and reported failures in its own words in `mod.log`. After a patch there was no single answer to "what broke?" except starting the game.

sco-core makes every address a named row in a table, resolves all rows in one place and writes one report. The same tables run against an `.exe` on disk, so a new game build can be checked before anyone plays it ([`sco-sigcheck`](sigcheck.md)).

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

- **Pattern row.** `pattern` must match exactly once in `.text`. The address is the match, or, with `ripDisp` and `ripSize`, the target of a RIP-relative operand inside the match (for example the global behind `mov rax, [rip+X]`).
- **Resolver row.** `resolve` gets the image and the registry's `SigResolveContext` and returns a `SigResult`. Use it when a pattern is not enough: starting from a string the function references, checking a dozen offsets inside a function, or deriving an address from another row.

`needs` lists the rows this row reads. A resolver calls `context.Sig()` only on ids in its own `needs`. It must not retain the context or throw.

Ids are `<feature>.<thing>` in lowercase with underscores: `teleport.to_camera`, `teleport.entity_system`, `system.quit`. The feature part matches the file under `src/game/` and the header under `include/sco/game/`. Ids are unique across all tables; registering a duplicate fails.

## Results

Each row ends in one state (`sco::SigState`):

| State | Report word | Means |
|---|---|---|
| `Ok` | `OK` | Found; `Sig(id)` returns the address |
| `Missing` | `MISSING` | Pattern row: 0 matches |
| `Ambiguous` | `AMBIG` | Pattern row: more than one match, so none is trusted |
| `Failed` | `FAILED` | The resolver said no, with a reason (`layout changed at +0x2b0`), or the row is broken (unknown need, detected cycle, no pattern and no resolver) |
| `Blocked` | `BLOCKED` | A row it needs is not OK; this row was not tried |
| `NotRun` | `NOT RUN` | The row has not been resolved in this pass |

`Sig(id)` returns an address only for `Ok` rows. It returns `nullptr` for every other state and for unknown ids, so a feature never gets a half-found address.

## Resolving

A `SignatureRegistry` copies the definitions it is given. One writer registers one or more tables, freezes the registry to link the `needs` graph, and resolves an image.

- `RegisterTables` registers a batch atomically. A failed batch commits nothing and returns a typed error. A failed call can keep container capacity, which stays charged to the registry budget.
- `Freeze` publishes no snapshot. `Snapshot()` is null until a resolve completes successfully.
- Resolution uses an explicit stack. It visits rows in registration order and a row's needs in their declared order. A need resolves before its dependent. An unknown need makes the row `Failed`. A need that is not OK makes the dependent `Blocked` without calling its resolver.
- A cycle is a failure that `Blocked` can overwrite as it propagates, so not every member of a cycle ends `Failed`.
- Each successful pass publishes a new immutable snapshot. A failed operation leaves the previous snapshot published. Callback side effects are not rolled back by a failed publication.
- An independent unsealed registry can resolve a different image.
- A snapshot lease keeps the definitions, result records and copied reasons alive. It does not keep the image behind an address or the resolver's code alive. The host must keep those alive, quiesce consumers and invalidate capabilities before teardown.
- Registry construction can throw on allocation; register, freeze and resolve return statuses. The byte budget counts the dynamic graph, index, resolution and snapshots, including retained old snapshots. Zero means limited only by the allocator. Fixed control objects, allocator overhead, images and callback allocations are not counted.

### Sealing and pins

Host composition seals a resolved registry before installing it. A sealed registry cannot be re-resolved. A fresh registry is needed for another host image.

Installation, clearing and signature-based capability commits serialize against the current owner. A pin keeps the owner (or its absence) stable across host work, and installation and ordinary clearing refuse while pins exist. A pin is a coordination count. It does not prove native readers have stopped.

Before game-service start and before any dependent teardown, the shell must hold a durable barrier that excludes native readers and target execution for the whole phase. The barrier must allow safe synchronous cleanup reentry and exclude the current quit control frame. `Platform::gameReadersQuiesced` confirms the barrier; a point-in-time observation is not enough. `Platform::beforeDependentStop` gates product consumers before teardown.

The registry stays pinned and the host fails closed when a required native barrier is missing, a product gate refuses, or audio, quantum or loadout cleanup fails. A process-retained quit hook or native DataCore detour keeps the registry and the borrowed image and provider code alive through process exit. Safe synchronization while installing product hooks has not been shown.

## The report

`sco::LogSignatureReport(*snapshot, false)` writes:

```
[core] signatures: 4/5 OK
[core] FAILED   teleport.to_camera (layout changed at +0x2b0)
```

The first line always appears. One line follows for each row that is not OK, in registration order. `LogSignatureReport(*snapshot, true)` also lists the OK rows. In the game this goes to `mod.log`; in `sco-sigcheck` it goes to the terminal.

## Startup

A host can have `sco::app::Start` create, freeze, resolve and install the game pack against the original image. The image and callback code must stay valid until dependent services have stopped and their uses of addresses have drained.

```cpp
sco::SetLogSink(ForwardCoreLog);
sco::app::Platform pf;
pf.image = &image;
pf.registerSignatures = sco::game::RegisterGameSignatures;
if (!sco::app::Start(pf)) { /* report setup failure */ }
```

With no image, a product can install its own resolved host registry before starting the kit. A registration, freeze, resolve or ownership failure refuses startup before any service or plugin starts. An individual missing row is recorded in the snapshot and leaves its capabilities unavailable. `setCapabilities` runs on the app game thread after the host owner is pinned.

Call `app::Stop` on the game thread. Keep the image and providers alive until teardown is complete. If game-service startup fails or throws, the app does not assume the partial provider installation is quiescent: it keeps its backing state, refuses restart, and the image and provider code must stay valid until process exit. See [the host kit](api.md#scoapph-the-host-kit).

**Plugin load order.** Inside `Start`, built-in plugins load first, in the product's list order. Discovered plugins (native and Lua; data packs are indexed after the loads) follow in folder order, except that a plugin loads after every plugin its `requires` names, by id (`requires = b`) or through one of its services (`requires = b.svc`). A cycle, or a name nobody provides, refuses the plugin. Just before each load the host re-checks that every provider is `Loaded`, so a provider that fails to load refuses its dependents. Unload runs in reverse load order. Details: [Plugins: load order](plugins.md#load-order-and-requires).

**Images.** `sco::ModuleImage()` (Windows only) reads the running executable's PE headers to find `.text` and `.rdata`. In `sco-sigcheck`, `sco::FileImage::Load()` builds the same `Image` from the file on disk by copying each section to its RVA, so every scanner and resolver sees the same bytes at the same offsets. No relocations are applied and no game code runs; addresses are only compared.

## The status channel

Features tell the player what happened through `sco::Status("Spawning %s...", name)`. It stores the message and writes `[status] Spawning ...` to the log. The menu shows the latest message with `sco::GetStatus()`. Features do not include menu headers, so feature code and UI code can change separately.

## Threading

| Call | Thread safety |
|---|---|
| Registry registration, freeze, resolve and sealing | One writer per registry; do not destroy it concurrently with a call on it |
| Snapshot acquisition and reads | Published snapshots are immutable and can be acquired and read on other threads; a lease does not extend the image lifetime |
| `Sig`, `SigReady` | Read the installed host snapshot; the host must coordinate image lifetime and shutdown with users of returned addresses |
| `Status`, `GetStatus` | Any thread (mutex) |
| `SetLogSink`, `Log` | Any thread (atomic sink); the sink itself must be thread-safe |
| `Post`, `Subscribe`, `Unsubscribe`, `RegisterCommand`, `ListCommands`, `Invoke` | Any thread. Tasks, event callbacks and commands always run on the game thread |
| `GameThreadTick`, `DrainTasks`, `Dispatch`, `Release` | Game thread only (the thread that called `SetGameThread`); elsewhere they run nothing. `GameThreadTick` and `DrainTasks` also refuse to run inside a task, callback or command |

## The runtime

[`sco/runtime.h`](../include/sco/runtime.h) is how work reaches the game thread. The host calls `SetGameThread()` once from the game's main thread and `GameThreadTick(nowMs)` on every main-thread tick. Each tick runs the tasks queued with `Post()`, in order, then dispatches the `tick` event.

- **Task queue.** A fixed ring of 256 tasks is the fast path: posting into it never allocates. When the ring is full, posts go to an overflow list under the same lock, and while the overflow holds anything new posts go there too, so tasks run in posting order across both. Each drain moves what fits back into the ring. The total waiting is capped at 65,536 (`kMaxQueuedTasksHard`). Past the cap, or out of memory, `Post` refuses with `TooMany` and queues nothing. A drain runs only the tasks queued when it started; tasks posted while it runs (a task re-posting itself too) wait for the next tick. `Release` drops an owner's tasks from both stages without allocating.
- **Event bus.** Subscribers are keyed by owner, event name and callback. The subscriber list is replaced on every change and a dispatch walks the list it started with, so changes never invalidate the walk. A new subscriber is called from the next dispatch. A removed one is flagged and skipped at once, even by a dispatch already running. Tasks never run during a dispatch, so a task posted after `Unsubscribe` is the safe place to free `ctx`.
- **Commands.** Features register named actions (`spawn.ship`) with typed arguments. `Invoke()` checks the argument count and types and the command's capability before calling it. On the game thread it runs at once. From another thread it copies the name and arguments, queues a task, and reports the result through the `done` callback on the game thread. The registry copies each command's strings and argument definitions into a slot. A slot never moves or is freed, and `Release(owner)` hands the owner's slots back for the next registration, so a `ListCommands()` pointer describes its command until its owner is released. After that the memory stays mapped but may describe a later command.
- **Owners.** Subscriptions, commands, tasks and queued `Invoke` calls carry an owner handle. `Release(owner)` removes them all at once and is final: later calls naming that owner are refused, so nothing it adds can outlive it. Unloading or disabling a plugin needs exactly this.
- **Services and faults.** `QueryService` hands the caller the provider's own raw table; the runtime does not mediate calls through it. The Windows guard blames the first plugin DLL in the faulting thread's frames, which is not necessarily the owner of the callout (built-ins share the product's module and keep owner blame). The loader first quarantines the owner: lookup, registration and host scheduling are withdrawn, but cached raw tables are neither revoked nor made safe.
  - Without a handed-out table, resource hooks run and the plugin becomes `crashed`.
  - With a handed-out table, the plugin becomes terminal `faulted`. No unload export or resource-release hooks run, its backing state and code stay retained (also through `UnloadAll`), and consumers must stop using cached tables and restart the game.
  - An administrative live-session unload or reload of an exposed provider is refused before teardown. Terminal host shutdown closes runtime admission before it unloads plugins that are still loaded.
  - Keeping a DLL mapped after ordinary shutdown does not guarantee its table stays callable.

  Details: [Plugins: crash containment](plugins.md#crash-containment), [unloading](plugins.md#unloading). The opt-in provider-side guard is not built.

The runtime is C++ and internal (version 0). The plain-C [`sco_api.h`](../include/sco_api.h) is a thin layer over it: `Result` and `ArgType` share their numbers with `sco_result` and `sco_arg_type`, and `Arg` and `ArgDef` share their layout with `sco_arg` and `sco_arg_def` (checked at compile time). `Command` is not `sco_command`: the host table ([`sco/host.h`](../include/sco/host.h)) builds `sco_command` views of registered commands for `list_commands`.

**Hot reload.** `sco::app::Reload(id)` (also the host command `sco.reload`) reloads the target and every transitively loaded dependent, using the discovered `requires` edges. It checks the whole group first and refuses before teardown if any member is non-reloadable, busy, missing requirements or has handed out a service table. It unloads dependents before providers and reloads providers before dependents, each under a new handle: natives from fresh DLL copies, Lua plugins by running their scripts again. Unrelated plugins stay loaded. Details: [Plugins: hot reload](plugins.md#hot-reload).

## Host diagnostics snapshot

[`sco/diagnostics.h`](../include/sco/diagnostics.h) is an internal C++ observation facility, not a plugin ABI or RPC. It adds no diagnostics channel, credential, MCP registration or product authorization, and the frozen `sco_api` is unchanged. Runtime inspection is a separate switch, `Platform::runtimeInspection`, and it is off by default. The trusted product owns the offline verdict. An unknown, online or denied verdict cannot create a mapping or credential. Collector I/O runs on a diagnostics worker, never on the tick. `Stop` revokes access before teardown and joins the bounded worker. See the [audit policy](runtime-audit.md).

- **Cadence.** `sco::app::Start` begins a copy after startup. `Tick` advances it on the game thread and begins at most one refresh per second. `Stop` invalidates the store before `game.exit` and plugin teardown. The any-thread `DiagnosticSnapshot()` accessor returns only a completed `shared_ptr<const Snapshot>`. Workers never traverse `app::Plugins()` and never ask the game thread to refresh on demand.
- **Construction.** Each call advances at most four work units and checks a soft 1 ms budget between units. One bounded registry copy and the final sort are individual units, so existing mutex contention cannot be promised to stay under 1 ms.
- **What a snapshot is.** An interval copy, not a simultaneous sample of all registries. Its timestamp is the earliest source time, so a slow build stays visibly stale. Only a complete, sorted generation becomes visible. Two generations and 64 local continuation tokens are kept. Tokens are lookup-only, bound to tool, filter and generation, invalidated by session reset, and expire after 60 seconds. An expired generation returns `snapshot_changed`, not page one. Inventories cap at 1024 and carry truncation and loss flags.
- **Service metadata** is copied under the existing registry mutex without table reads or `QueryService`. Withdrawal history is bounded and best-effort, and an allocation failure while observing never rejects a successful publication or withdrawal. "Exposed" means the owner has handed out any table, not a counted consumer lease. Owner, category and size stay unknown unless trusted host code registers them, and even a registered size is a metadata assertion, never a size-prefix probe. Signature state comes from existing results, with no resolver or scanner run and no addresses or patterns. Load and resolution generations are reported unknown instead of substituting load order or the snapshot generation. Capability gates are copied, never tested by invoking a method.
- **Reserved slots.** The optional `GamePackMetadata` and `DiagnosticsPluginMetadata` source slots are absent by default. They reserve space for [#141](https://github.com/scuba-sc/sco-core/issues/141), not for publishers, gameplay records, game types or new plugin-authored JSON fields. Capture is a separate keyed host-only facility that stays inactive until started, and nothing installs it into a product or the ordinary logger. See [log privacy](log-privacy.md) and the [phased design](design/runtime-mcp.md#historical-phased-pr-plan-and-acceptance).

The synthetic acceptance test is `test_runtime_diagnostics_snapshot`. Timing under Windows, protected audit and security permissions, and shutdown and tick latency have not been verified. Not run in game.

## Limits

| Limit | Value | What happens at the limit |
|---|---|---|
| Signature rows | No fixed count; an optional `SignatureRegistry` byte budget, 0 meaning limited only by the allocator | Registration or resolution returns a resource status; a failed batch commits nothing and a failed resolve keeps the previous snapshot |
| Needs per row | 4 (`kMaxSigNeeds`) | Compile error |
| Pattern length | 96 bytes (`kMaxPatternBytes`) | The pattern matches nothing (`MISSING`), never a shortened match |
| Log line | 511 characters | Truncated |
| Status message | 255 characters | Truncated |
| Queued tasks | 65,536 in all (`kMaxQueuedTasksHard`); the first 256 (`kMaxQueuedTasks`) in an allocation-free ring, the rest in an overflow | `Post` returns `TooMany` |
| Event subscriptions | 512 (`kMaxSubscriptions`) | `Subscribe` returns `TooMany` |
| Commands | 512 live (`kMaxCommands`); `Release(owner)` frees its owner's slots, so loading and unloading plugins any number of times never exhausts them | `RegisterCommand` returns `TooMany` |
| Plugin handles | 256 (`host::kMaxPlugins`) for the life of the process; every load counts, reloads too | `NewPlugin` returns null; a reload leaves the plugin unloaded |
| Command strings | name, title, capability 63; help 255; argument name 31; argument help 127 | `RegisterCommand` returns `BadArg` |
| Arguments per command | 16 (`kMaxCommandArgs`) | `RegisterCommand` and `Invoke` return `BadArg` |
| Command reply | 255 characters | Truncated |

## Layers

sco-core has five layers. Each depends only on the ones below it. The first two are the kernel build, and CI's `kernel-only` job fails if they reach into the game pack.

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

- **Kernel.** Game-agnostic. It knows nothing about Star Citizen. A host for another game links the kernel targets and passes its own signature tables.
- **Host services.** The host publishes them under the reserved id `sco` before any plugin loads and withdraws them after the last unload ([API v1](api-v1.md#host-owned-services)). They are game-agnostic, so they live in the kernel build. `sco.datacore` is also published under `sco`, but it belongs to the game pack: it patches Star Citizen's DataCore, so it exists only with `SCO_GAME_SC`.
- **Game pack.** Everything that knows Star Citizen. Its services are plain `query_service` tables under the owner `game` ([game services](game-services.md)). A game patch is fixed here, once, in the rows ([adding a signature](adding-signatures.md)).
- **Plugin SDK.** The contract plugins build on: `sco_api.h` (stable since sdk-v1.1.0; version 1 only grows), the per-service headers, each pinned by an `abi_*.c` test, the language layers and the zip that ships them ([plugin SDK](../sdk/README.md)). A header's prefix names its layer: `sco_` is a host service, `sc_` a game-pack or product service or wire.
- **Consumers.** Products and plugins. sc-offline links `sco_app` and `sco_pak` (which bring the game pack) and runs its features as built-in plugins on the same ABI as third-party ones.

## Kernel and game pack

sco-core builds as a game-agnostic kernel plus one game pack, Star Citizen's. A build option enforces the boundary and CI checks it.

- **Kernel:** `sco_api.h` and the runtime (`src/api`), the host table (`src/host`), plugins and the host kit (`src/plugins`, `src/app`), the host services `sco.storage`, `sco.ui`, `sco.settings`, `sco.ipc` and `sco.net`, the hook engine, the VFS engine, the zone tree (`src/engine`), the signature engine (`sco_scan.cpp`, `sco_signatures.cpp`, `sco_pe_file.cpp`) and sco-lua.
- **Star Citizen game pack** (`SCO_GAME_SC`, on by default): every row in `src/game/*_sigs.cpp` and `sco::game::RegisterGameSignatures` (`sco_game_sc`), the CryPak adapter (`sco_pak`), DataCore and `sco.datacore` (`sco_datacore`, `sco_datacore_service`), `sco-sigcheck` and `sco-dcb`. The SDK headers for game-pack services (`sc_*.h` and `sco_datacore.h`) stay in `include/` and in the SDK zip, so plugins built against sdk-v1.1.0 keep working.
- **The gate.** CI's `kernel-only` job (`cmake.yml`) configures with `-DSCO_GAME_SC=OFF`, checks that no game-pack object or symbol was built, and runs the tests. Where the kernel can use a game-pack service when one is there (the host kit's `Platform::dataCore`, sco-lua's `sco.datacore` binding, `sco-host-sim`'s pack lint), the code is guarded by `SCO_KERNEL_ONLY` or finds the service at run time.
- **A product** picks the pack's parts. sc-offline links `sco_app` and `sco_pak`, registers the game tables itself before its patches, and starts the host kit with `Platform::dataCore`. A host for another game links the kernel targets and passes its own tables as `Platform::registerSignatures`.

### Game services

The game pack publishes its services under the reserved owner `game` with `host::ProvideGameService` (the sibling of `ProvideHostService`). The code is in `src/game/services/` (library `sco_game_services`, Windows only, because game reads run under SEH). A product turns the services on with `Platform::gameServices`; a build without them logs `[app] game services not built` and goes on.

Every table has the same shape: size first, `self` first on every function that changes the game, caller buffers, `last_error`, one capability per system, and a pin in `tests/abi_game_<name>.c`.

- **Versions, capabilities and status.** [Game services](game-services.md) lists every table with its version and capabilities. [Status](game/status.md) says what is on, what is off and what evidence exists.
- **Events.** Hooks on the game's own event queues record into a fixed ring and never run plugin code. The host's tick drains the ring and dispatches through the kernel event bus on the game thread. A hook installs only when its capability's rows are OK. The rows are in `src/game/events_sigs.cpp`.
- **Release hook.** One release hook (`spawn.cpp`'s `OnRelease`, registered with `sco::AddReleaseHook`) frees the per-plugin state of `spawn.entities`, `game.actors` and `game.entities`. An ordinary unload (reload included) or a non-retained `crashed` cleanup removes the NPCs and entities the plugin spawned, ends its watches and forgets spawn ownership. An entity the plugin `keep`s is taken off its ownership list and stays. A restart-required retained-provider fault skips the hook and keeps the backing resources while host callbacks are suppressed.
- **Not services.** The same library holds internal helpers a product calls directly, such as `sco::game::missions::ScriptLibrary` (the mission script library lookup, `sco/game/missions.h`).
- **Version line.** The game pack has its own version, `SCO_GAME_PACK_VERSION` in [`sc_game_pack.h`](../include/sc_game_pack.h) (0.8.1 in SDK 2.4.1), with tags `game-sc-vX.Y`. Verified builds are in [the game pack page](game-pack.md).

## Source map

| File | What |
|---|---|
| `src/sco_scan.cpp` | `FindPattern`, `FindCString`, `FindRipLea`, `BytesMatch`, `Rel32` |
| `src/sco_signatures.cpp`, `src/sco_signature_registry.cpp` | Rows, resolver, report; the registry and its snapshots |
| `src/sco_log_status.cpp` | Log sink and status message |
| `src/sco_image_win.cpp` | `ModuleImage()` for the running game (Windows) |
| `src/sco_pe_file.cpp` | `FileImage::Load()` for host tools |
| `src/api/sco_tasks.cpp` | Game-thread identity, task queue, `GameThreadTick`, `Release` |
| `src/api/sco_events.cpp` | Event bus |
| `src/api/sco_commands.cpp` | Command registry and `Invoke` |
| `src/api/sco_services.cpp` | Service registry (`ProvideService`, `QueryService`) |
| `src/api/sco_caps.cpp` | Capability registry |
| `src/api/internal.h` | Callout depth and the per-module halves of `Release`, shared by `src/api/` |
| `src/hook/sco_hook.cpp` | Near caves, `WriteCode`, the detour registry (`sco/hook.h`) |
| `include/sco/engine/types.h` | `Vector3d`, `Quatd`, `Transform`: header-only 64-bit spatial math, XYZW helpers for the game's quaternion order |
| `src/engine/zone.cpp` | `ZoneTree` (`sco/engine/zone.h`): zones the host feeds, transforms between them |
| `src/host/sco_host.cpp` | The host's `sco_api` table and per-plugin handles |
| `src/plugins/`, `src/app/` | Plugin discovery, loading, crash containment, hot reload; the host kit (`sco/app.h`) |
| `src/settings/settings.cpp` | `sco.settings` (`sco/settings.h`): typed `[settings]` values kept in `sco.storage`, the read-only table, `settings.changed`, the pages the product draws |
| `src/plugins/settings_ini.cpp` | The `[settings]` grammar of `plugin.ini`; reading, checking and writing one value (`sco/plugins.h`) |
| `src/storage/`, `src/ui/`, `src/widgets/`, `src/content/`, `src/vfs/` | `sco.storage`, `sco.ui` and `sco.widgets`, the content index, the VFS engine |
| `src/ipc/`, `src/net/` | `sco.ipc` and `sco.net` ([IPC](ipc.md), [multiplayer messages](net.md)) |
| `src/datacore/datacore.cpp` | `sco::datacore::Schema::Parse` (`sco/datacore.h`): the DataCore layout and its validation |
| `src/game/signatures.cpp` | `RegisterGameSignatures(registry)`: the authoritative game table list |
| `src/game/<feature>_sigs.cpp` | One feature's rows and its typed accessor |
| `src/game/services/*.cpp` | The game services and events, the shared game reads (`reads.cpp`) and the mission script library lookup (`missions.cpp`); library `sco_game_services` |
| `tools/sco-sigcheck.cpp` | The offline checker |
| `tools/sco-dcb.cpp` | `sco-dcb` over a `Game2.dcb` on disk |
| `tests/` | `test_core.cpp` (synthetic image), `test_runtime.cpp`, `test_host.cpp`, `test_spatial.cpp` (ASan+UBSan and ThreadSanitizer), `test_hook.cpp` (detours over small functions written into executable memory, x86-64), `test_datacore.cpp` with `dcb_builder.h` (synthetic DataCore files written independently of the parser) |
