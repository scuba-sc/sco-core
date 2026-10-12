# Design: service-table lifetime audit

Status: source audit of `0ce4ab8`, updated for atomic administrative withdrawal, built-in provider refusal, queried failed-load retention and dependent reload cascades. No plugin ABI growth. This is an inventory of the current implementation, not a lease or crash-safety guarantee. See [service safety](service-safety.md) for the historical proposal and [plugins](../plugins.md#hot-reload) for the supported reload contract.

## Scope and result

A service table is the opaque pointer published through `provide_service`, not a signature-row table, VFS mount table, command-list view or IPC data mapping. The inventory covers core, game pack, SDK accessors and native provider lifecycle. Source locations in the census are repository-relative definition/registration lines from the original audited tree. Updated lifecycle paths are identified by function name, not stale loader line numbers.

- **13 built-in named tables:** six owned by the host token and seven by the game-pack token. These are compiled-in services, not `plugins::Kind::Builtin` instances.
- **Four mid-session entry functions:** `ReloadCommand`, `app::Reload`, `UnloadOne`, `UnloadNative`. All four reach the single `UnloadGate` in `src/plugins/loader.cpp`. `CanUnload` is only an advisory inspection; `BeginUnload` atomically closes query/publication admission before any teardown. Built-in plugin owners follow the same rule as native owners.
- **Failed-load cleanup also reaches `UnloadGate`:** if a table was queried during load, the loader refuses the load and closes new admission, but retains backing state, exports, owner resources and native code. No unload export, owner release or script-runtime unload runs. Host callouts are suppressed by `ContainCallouts` for Refused entries. Shutdown, faults, explicit withdrawal and direct runtime owner release remain separate operations with the limits below.
- `app::Reload` now includes transitively loaded `requires` dependents, using resolved `Plugin::after` edges. It reserves the whole group before copies or teardown, unloads dependents before providers and reloads providers first. Busy, handed-out or non-reloadable members refuse the entire group. There is still no file watcher or dedicated C/UI reload entry; UI and C callers invoke `sco.reload`.

## Ownership and what a hand-out protects

`ProvideService` (`src/api/sco_services.cpp:42`) stores the provider's pointer verbatim. It copies the name, not the table or the table's backing state. `QueryService` (`:61`) holds the registry mutex, checks the version and records the owner in `g_handedOut` **before** returning `s.table`. A recording allocation failure returns `TooMany`, with no pointer. `ServiceExists` (`:82`) and `ServiceTableHandedOut` (`:90`) return booleans, not tables.

The hand-out mark survives `ReleaseService` and owner release. The unit is **owner**, not service or consumer: one successful lookup, even by the provider itself, makes every table of that native owner session-lifetime for administrative unload/reload. Owner handles are never reused (`host::NewPlugin`, `src/host/sco_host.cpp:296`). There is no release-reference API; `release_service` only withdraws a name. `ServiceRef` in the SDK is a pointer holder, not a counted reference.

`UnloadGate` (`src/plugins/loader.cpp`) applies to every plugin owner with a `self`, including `Kind::Builtin`. A refusal precedes unload exports and `Release`, leaving a loaded provider intact. `KeepMapped`, used by `CloseOrKeep`, remains a separate last-line module pin after withdrawal. **A mapped DLL is not proof that heap-allocated table storage, plugin objects or game state are still alive.** Crash and shutdown consumers must not infer that a stale raw table is callable.

Registry operations are any-thread and mutex-protected. Plugin lifecycle/list mutation is a **game-thread caller contract** (`include/sco/plugins.h`); most loader functions do not independently reject a wrong-thread call. `app::Reload` does reject one (`src/app/sco_app.cpp:235`). A lookup's thread permission never relaxes a table entry's own thread rule.

## Atomic admission and failed-load retention

`ServiceWithdrawal` (`src/api/sco_services.cpp`) holds the same mutex as `QueryService` and `ProvideService`. It inspects the whole owner group and records every withdrawing owner before releasing the lock. Administrative refusal or allocation failure leaves the whole group unchanged. A successful reservation prevents both new queries and republication (`Unavailable`, null query output), including calls synchronously inside an unload export or on a joined worker. An earlier successful query records its hand-out before the gate can run, so the gate refuses teardown instead. The mutex is never held while plugin code runs.

`CanUnload` calls `UnloadGate` in inspection mode; every actual unload rechecks using withdrawal mode. `app::Reload` uses `BeginUnload` for its entire dependent group, then `UnloadOne` reuses those reservations. Copy failure cancels reservations without teardown. An unload failure stops before that dependent's providers are touched; only still-Loaded survivors have their reservations cancelled. Released, Refused and Crashed owners are not reopened by cancellation.

A non-OK native/built-in load or script-runtime load uses failed-load mode: close admission even when already handed out, and refuse destructive cleanup if so. The entry stays Refused with a retention reason; cached pointers are not revoked. SDK/provider code may have already invalidated its own state inside load, independently of the host. This is a host cleanup guarantee, not a guarantee that an arbitrary failed provider remains callable.

## Built-in table census

H = `host::HostOwner()` (`src/host/sco_host.cpp:316`), the permanent `&g_hostOwner` token under reserved id `sco`. G = `host::GameOwner()` (`:337`), permanent `&g_gameOwner` under `game`. Neither token is released; registrations and backing resources are withdrawn/stopped separately. `sco.datacore` is H-owned even though its implementation belongs to the optional game pack. Compatibility names `teleport.spatial` and `spawn.entities` are G-owned, not plugin-owned aliases.

All objects below have static table storage and host/game-pack code lifetime. Start/Stop do **not** run `CanUnload`: their owners are not unloadable native plugins. Normal `app::Start` starts these before plugins (`src/app/sco_app.cpp:93–124`); `app::Stop` unloads plugins before stopping them (`:206–225`). A stable address does not authorize use after Stop or across a new host session.

| Service; version | Owner; static table definition | Publish function / line | Teardown function / line | Entry thread rule |
|---|---|---|---|---|
| `sco.storage`; 1.0 | H; `kTable`, `src/storage/storage.cpp:753` | `Start`, :788 / :802 | `Stop`, :812 / withdraw :820 | Any thread; DB calls serialized per plugin |
| `sco.settings`; 1.0 | H; `kTable`, `src/settings/settings.cpp:231` | `Start`, :302 / :310 | `Stop`, :320 / withdraw :328 | Any thread reads; host setters game thread |
| `sco.ui`; 1.0 | H; `kTable`, `src/ui/ui.cpp:424` | `Start`, :467 / :475 | `Stop`, :485 / withdraw :496 | Any thread registration; draw callbacks game thread |
| `sco.ipc`; 1.0 | H; `kTable`, `src/ipc/ipc.cpp:350` | `Start`, :379 / :396 | `Stop`, :406 / withdraw :412 | Any thread; channel locking |
| `sco.net`; 1.0 | H; `kTable`, `src/net/service.cpp:604` | `Start`, :655 / :675 | `Stop`, :689 / withdraw :703 | Any thread service calls; callbacks from game-thread Tick, never worker |
| `sco.datacore`; 1.1 | H; `kTable`, `src/datacore/service.cpp:385` | `Start`, :427 / :442 | `Stop`, :452 / withdraw :460 | Any thread; service mutex |
| `teleport.spatial`; 1.0 | G; `kSpatial`, `src/game/services/spatial.cpp:231` | `services::Start`, :239 / :242 | `services::Stop`, :319 / withdraw :329 | Game thread; 0 when not ready/wrong thread |
| `spawn.entities`; 1.2 | G; `kService`, `src/game/services/spawn.cpp:341` | `StartSpawn`, :348 / :352 | `StopSpawn`, :365 / withdraw :369 | Game thread; 0 on refusal |
| `game.actors`; 1.1 | G; `kActors`, `src/game/services/actors.cpp:557` | `StartActors`, :567 / :576 | `StopActors`, :597 / withdraw :607 | Game thread; `last_error` any thread |
| `game.vehicles`; 1.0 | G; `kService`, `src/game/services/vehicles.cpp:664` | `StartVehicles`, :670 / :677 | `StopVehicles`, :686 / withdraw :688 | Game thread; `last_error` any thread |
| `game.world`; 1.0 | G; `kWorld`, `src/game/services/world.cpp:343` | `StartWorld`, :351 / :354 | `StopWorld`, :367 / withdraw :369 | Game thread; `last_error` any thread |
| `game.entities`; 1.1 | G; `kEntities`, `src/game/services/entities.cpp:996` | `StartEntities`, :1014 / :1031 | `StopEntities`, :1062 / withdraw :1075 | Game thread (`alive` returns 0 off-thread); `last_error` any thread |
| `game.creative`; 1.0 | G; `kService`, `src/game/services/creative.cpp:617` | `StartCreative`, :638 / :643 | `StopCreative`, :658 / withdraw :662 | Game thread; `last_error` any thread |

Host service Start/Stop may be called on any thread according to their headers; the host kit serializes lifecycle on the game thread. Game service Start/Stop require the game thread (`include/sco/game/services.h:22`). `services::Start` rolls back already-started siblings on a later failure (`spatial.cpp:244–310`), and `services::Stop` stops them in reverse (`:319`). Those are compiled-in resource teardown paths, not DLL unloads. UI frame and network message pointers are callback-only, independently of table lifetime.

### Native and built-in plugin providers

`host::ProvideServiceC` (`src/host/sco_host.cpp`) publishes with the plugin's `self` owner and id prefix. `Kind::Builtin` may publish by the same route; its code never unmaps, but its unload function can destroy backing state. Direct `UnloadOne` now refuses a built-in whose table was handed out. `app::Reload` still rejects built-in targets and rejects a cascade containing a built-in dependent. No concrete product built-in table is defined here: products supply `Platform::builtins`. The loader cannot enumerate arbitrary third-party table layouts or prove provider-managed storage lifetime.

The repository's native example providers are `cpp_hello.greeter` (`sdk/examples/cpp_hello/cpp_hello.cpp:31,62`: static `kGreeter`) and `cs_hello.greeter` (`sdk/examples/cs_hello/CsHello.cs:35,68–74`: a once-allocated, never-freed NativeMemory table). Both publish 1.0 under their native plugin owner; their greet function is independent of per-plugin mutable state. Their lifecycle/export entry points are game-thread calls; raw service calls have no host dispatcher. Tests additionally publish synthetic `svc_provider`/`svc_prov_b/c/d.svc` (`tests/fixtures/plugins/native/fake_plugin.c`, mode 11) and stand-in tables; these are fixtures, not shipped host services. Lua exposes no `provide_service` binding (`plugins/lua/sco_lua.c:1233`).

## Every table-returning surface

The primary C query writes an out-pointer; cached getters return it directly. These surfaces do not themselves call `CanUnload` (which belongs to provider lifecycle).

| Function / accessor | Location | Pointer owner and lifetime; thread rule |
|---|---|---|
| `sco::QueryService` | `src/api/sco_services.cpp:61` | Raw provider table; hand-out tracked under registry lock, any thread |
| `host::QueryServiceC` / `sco_api.query_service` | `src/host/sco_host.cpp:228`, wired :287; `include/sco_api.h:204` | Delegates to QueryService; same owner/rules |
| `storage::Table` | `src/storage/storage.cpp:830` | H static table, unconditional even when stopped; any thread |
| `settings::Table` | `src/settings/settings.cpp:337` | H static table, unconditional even when stopped; any thread |
| `ui::Table` | `src/ui/ui.cpp:505` | H static table, unconditional even when stopped; any thread |
| `ipc::Table` | `src/ipc/ipc.cpp:436` | H static table, unconditional even when stopped; any thread |
| `net::Table` | `src/net/service.cpp:871` | H static table, unconditional even when stopped; any thread |
| `datacore::service::Table` | `src/datacore/service.cpp:469` | H static table, unconditional even when stopped; any thread |
| `host::BuildApi` | `src/host/sco_host.cpp:269` | Root host `g_api`, not named service; process-static, call_once initialization; startup/game-thread contract |
| C++ `Plugin::Api` | `include/scosdk/plugin.hpp:290` | Borrowed root API, OnLoad through OnUnload return; no new lookup |
| C++ `ServiceRef<T>::Query` (both overloads), `Get`, `operator->` | `include/scosdk/service.hpp:62,71,75,76` | Query populates cached provider pointer; getters hand it back. Any-thread query; synchronize mutation of the wrapper itself |
| C# `Plugin.Api` | `sdk/csharp/Sco.Sdk/Plugin.cs:55` | Borrowed root API; cleared by SDK release |
| C# `Plugin.Query<T>`, `ServiceRef<T>.Table` | `sdk/csharp/Sco.Sdk/Services.cs:76,31` | Query outputs pointer holder; Table returns cached provider pointer, no lease; lookup any thread |
| C# `Storage.Table`, `DataCore.Table`, `DataCorePatch.T` | `sdk/csharp/Sco.Sdk/Storage.cs:189`; `DataCore.cs:96,115` | Internal/private cached H pointers; underlying service thread rules |
| Lua `sco_lua_api` | `plugins/lua/sco_lua.c:121` | Script's borrowed root API; script/game-thread lifetime, not a service query |

The six native `Table()` getters bypass registry presence/version/hand-out checks; they cannot return an unloadable plugin's table. Their headers promise a process-lifetime address, not process-lifetime resources.

### C++ typed cached getters

All paths below are under `include/scosdk/`. Each `Open` calls `query_service`; each `Table` returns the cached pointer and inherits the service's thread/owner rules. `Open` does not confer ownership or safe use across teardown.

| Wrapper | `Open` definition line(s) | Raw getter line |
|---|---|---|
| `Settings` | `settings.hpp:42,44` | :56 |
| `Storage` | `storage.hpp:154,156` | :168 |
| `Ui` | `ui.hpp:47,49` | :61 |
| `Ipc` | `ipc.hpp:148,149` | :161 |
| `Net` | `net.hpp:94,95` | :107 |
| `game::Actors` | `game/actors.hpp:35,37` | :49 |
| `game::Entities` | `game/entities.hpp:37,39` | :51 |
| `game::World` | `game/world.hpp:35,37` | :49 |
| `game::Vehicles` | `game/vehicles.hpp:36` | :41 |
| `game::Creative` | `game/creative.hpp:34,35` | :41 |

`DataCore::Open` (`datacore.hpp:237,238`) retains a `ServiceRef` without a public Table getter; `Begin` (`:252`) returns a patch wrapper retaining that pointer. `AddRecordSupported` (`:247`) makes another query but returns a boolean. C# typed `Open` functions likewise cache Query results: `Settings.cs:26`, `Storage.cs:54`, `Ui.cs:30`, `DataCore.cs:70`, `Game/Actors.cs:61`, `Game/Entities.cs:63`, `Game/World.cs:75`, `Game/Vehicles.cs:83`, `Game/Creative.cs:60` (all under `sdk/csharp/Sco.Sdk/`). They return status, not raw pointers. No typed C# IPC/Net layer is present.

### Lua C helpers

These native helpers return a queried pointer internally, on the script/game thread; Lua code does not receive a raw service pointer. Each operation requeries rather than holding a general service cache. The pointer belongs to H or G as in the census.

| Helper | Location |
|---|---|
| `Settings` | `plugins/lua/sco_lua.c:690` |
| `Storage`, `StoreCall` | `plugins/lua/sco_lua.c:748,782` |
| `UiTable` | `plugins/lua/sco_lua.c:1088` |
| `DataCore` | `plugins/lua/sco_lua_game.c:65` |
| `ActorsTable` | `plugins/lua/sco_lua_game.c:276` |
| `Entities` | `plugins/lua/sco_lua_game.c:306` |
| `Vehicles` | `plugins/lua/sco_lua_game.c:385` |
| `Creative` | `plugins/lua/sco_lua_game.c:474` |
| `WorldTable` | `plugins/lua/sco_lua_game.c:508` |

## Unload, reload and teardown path inventory

N = native plugin owner (`p.self`); B = built-in plugin owner; L = script owner. H/G as above. The table separates request entry functions from internal sinks; counting every wrapper as a separate independent safety check would overstate coverage.

| Function/path; location | Owner; thread rule | Gate / effect |
|---|---|---|
| `ReloadCommand`, `src/app/sco_app.cpp` | N/L; game thread | `Reload` -> `BeginUnload` -> `UnloadGate` for the entire cascade |
| `app::Reload`, `src/app/sco_app.cpp` | N/L; game thread enforced | Group inspection, atomic reservation, then each `UnloadOne`; B/data members rejected |
| `UnloadNative`, `src/plugins/loader.cpp` | N; game-thread contract | `UnloadOne` -> `BeginUnload` -> `UnloadGate` |
| `UnloadOne`, `src/plugins/loader.cpp` | N/B/L; game-thread contract | Atomic `UnloadGate` before any export, runtime unload or Release |
| C API `invoke` / `host::InvokeC`, `src/host/sco_host.cpp` | Any thread, off-thread queued | `sco.reload` uses the same command path; no C lifecycle bypass |
| UI hotkey `ui::Dispatch`, `src/ui/ui.cpp` | Any thread, off-thread queued | Bound `sco.reload` uses the same command path; no UI lifecycle bypass |
| `UnloadOneNow`, `src/plugins/loader.cpp` | N/B/L; game thread | Private sink after administrative reservation; shutdown exception below |
| `UnloadCode`, `src/plugins/loader.cpp` | N/B; game thread | Private unload export, ReleaseOwner and CloseOrKeep; no separate predicate |
| `UnloadScript`, `src/plugins/loader.cpp` | L; game thread | Private Release/runtime unload sink after administrative reservation |
| `app::Stop`, `src/app/sco_app.cpp` | All plugin owners, then H/G | **Shutdown exception**, game.exit, UnloadAll, service Stops and withdrawals |
| `UnloadAll`, `src/plugins/loader.cpp` | Loaded N/B/L; game thread | **Shutdown exception**, UnloadOneNow, newest first and B last; queried N stays mapped |
| `QueryAndLoad`, `src/plugins/loader.cpp` | New N/B; game thread | Non-OK load -> `UnloadGate` failed-load mode; queried backing state retained; load fault -> Crash unchanged |
| `LoadNative`, `LoadBuiltin`, `src/plugins/loader.cpp` | New N/B; game thread | Ready-only load; shared QueryAndLoad cleanup. Pre-load export/info refusals have no newly published owner |
| `RefuseLoad`, `src/plugins/loader.cpp` | N/B; game thread | Clears exports and closes only after eligible cleanup; queried failed loads do not reach this sink |
| `CloseOrKeep`, `CloseModule`, `src/plugins/loader.cpp` | N | Final module pin, not a backing-state guarantee or alternative administrative unload |
| `LoadScript`, `src/plugins/loader.cpp` | L; game thread | Failure cleanup -> `UnloadGate` before Release or runtime unload; Lua cannot publish tables, but a host runtime can |
| `Crash`, `MarkCrashed`, `src/plugins/loader.cpp` | N/B/L; game thread | **Fault exception**, owner release and Crashed; module retained; unchanged here |
| `CallPlugin`, `ContainedCallout`, `src/plugins/loader.cpp` | Callout owner or faulting N | Fault -> MarkCrashed or BlameProvider; not administrative unload |
| `BlameProvider`, `BlameTask`, `BlameProviderNow`, `src/plugins/loader.cpp` | Faulting N | Off-thread verdict posted to game thread; queue-failure behavior unchanged |
| `ReleaseOwner`, `ReleaseFailed`, `src/plugins/loader.cpp` | N/B/L | Internal Release helpers used after gate or by explicit fault/shutdown exceptions |
| `sco::Release`, `src/api/sco_tasks.cpp` | Runtime owner; game thread enforced | Direct runtime cleanup, not a loader administrative request; release hooks may tear down resources |
| `ReleaseService`, `detail::ReleaseServices`, `src/api/sco_services.cpp` | Published owner; any thread, lock | Name withdrawal only; hand-out mark and loader reservation are preserved |
| `host::ReleaseServiceC`, `src/host/sco_host.cpp` | Valid self; any thread | Explicit service-name withdrawal, not unload |
| `WithdrawHostService(s)`, `WithdrawGameService(s)`, `src/host/sco_host.cpp` | H/G; any thread | Registry withdrawal, not backing-state teardown |
| Host/game service Stop functions | H/G; see census | Compiled-in resource teardown, not Kind::Builtin plugin unload; unchanged |
| `Disable`, `Failed`, `sco_lua_unload`, `Close`, `plugins/lua/sco_lua.c` | L; script/game thread | Lua-owned runtime state; no native table publication binding |
| `CopyForReload`, `RemoveReloadCopies`, `src/plugins/loader.cpp` | Filesystem copies | No provider teardown or ModuleOps.close |
| Cascading reload of `requires` dependents | `app::Reload`, game thread | Entire loaded dependent closure reserved through the single loader gate; reverse teardown, forward load |
| Automatic file watcher | None implemented | Not applicable |

Resource release hooks are additional teardown sinks, not table/module owners: `OnRelease` in `src/storage/storage.cpp:772`, `src/settings/settings.cpp:237`, `src/ui/ui.cpp:440`, `src/ipc/ipc.cpp:372`, `src/net/service.cpp:613`, `src/datacore/service.cpp:390`; game `spawn.cpp:263` delegates to `ReleaseActorsOwner` (`actors.cpp:649`), `ReleaseEntitiesOwner` (`entities.cpp:1095`), `ReleaseCreativeOwner` (`creative.cpp:666`). They run on the game thread through `sco::Release`; they close/despawn/reset the released consumer's resources, do not ask CanUnload and do not revoke cached tables held by other consumers.

## Regression evidence and limits

SDK provider-side teardown is reached from the guarded unload export, not through a second host predicate: C++ `detail::Slot<T>::Unload` (`include/scosdk/plugin.hpp:447`) calls `UnloadPlugin` (:695), then deletes the plugin object; `Plugin::Shutdown` (:657) clears SDK callback state, and failed `LoadPlugin` (:671) also calls it. `SCO_PLUGIN` emits the export at :724. C# `PluginExports.Unload` (`sdk/csharp/Sco.Sdk/Plugin.cs:446`) calls `UnloadPlugin` (:474), then `Plugin.Release` (:335) clears callbacks/arena/API; failed `LoadPlugin` (:458) also calls Release. These game-thread provider hooks do not consult CanUnload themselves, and a provider can still invalidate its own heap-backed table/state outside host lifecycle. They are not extra administrative host unload routes.

The standalone checker is a different one-plugin host: `sdk/tools/sco-plugin-check.c:891` always sets its query out-pointer null and returns NotFound/BadArg. It cannot hand out a provider table; its `check_native` shutdown calls the unload export at :1074 and intentionally leaves the module mapped. `tools/sco-host-sim.cpp:234` instead shuts down through `app::Stop`. Neither supplies a hidden live-session reload path.

`tests/test_plugins.cpp`: `TestServices` covers real-DLL refusal through both public unload routes and terminal module pinning. `TestAtomicUnloadAdmission` deterministically queries and attempts republication inside the unload export, with an additional joined worker query (no sleeps). `TestBuiltinServiceUnloadRefusal` proves unload exports and release hooks cannot destroy queried built-in backing state, even after explicit withdrawal/republication. `TestFailedLoadServiceRetention` covers native and built-in non-OK loads; `TestFailedScriptServiceRetention` covers a publishing host script runtime. Each new path was observed failing before its fix. `tests/test_app.cpp`: `TestReload` covers app, command, C API invoke and UI dispatch refusal; `TestReloadCascade` covers transitive/diamond ordering, whole-group reservation, copy rollback, busy and handed-out dependents, and partial-failure behavior; `TestReloadNonCodeDependents` covers built-in/data cascade refusal.

Local verification uses the kernel-only CMake `test_plugins` and `test_app` targets. That covers the refusal logic and real host shared-library loading, not the Windows SEH stack walk or game-pack Stop behavior in a running game. Not run in game. The full local `tools/test.sh` path is not used because of the known unrelated macOS warning-as-error failures in the VFS/pak code.

## Open: provider withdrawal, fault behavior and shutdown

The atomic administrative race, built-in plugin refusal and queried failed-load cleanup findings are repaired above. These remaining findings are not changed here:

- **Explicit withdrawal/republication:** release_service withdraws a name, not a reference. Outside a loader reservation, a provider may subsequently free/replace dynamic table memory while existing consumers retain it. The host gate cannot constrain arbitrary provider-managed teardown or replacement.
- **Fault behavior:** Crash releases provider-owned resources despite handed-out tables and keeps code mapped. There is no provider-call wrapper, lease, availability guard or stale-state guarantee. Off-thread calls outside a host guard are not contained; POSIX's default guard contains nothing. The queued provider-blame path differs from owner blame and is not general thread-safe lifecycle permission.
- **Shutdown ordering:** reverse declared load order and built-ins-last is not a graph of all actual queried dependencies. A consumer without a declared requires may unload after a provider. Stop is terminal for consumers; do not keep calling raw tables during/after shutdown based only on module pinning. Crash/killed-process shutdown is best effort.
- **Static getters and SDK caches:** native Table() getters return a non-null static address even after Stop. SDK copies do not automatically invalidate on withdrawal/fault/Stop. C# typed Open methods can retain the old pointer on failed reopen; lookup-again is not a lease either. Do not strengthen this into an all-functions-return-Unavailable claim: UI chord normalization is pure, DataCore state is a state value, and Net predicates can return 0.

`sco_api.h`, service table layouts, fault policy, shutdown ordering and SDK caches are unchanged. Dependent reload policy changes only as described above; raw table references are never revoked.
