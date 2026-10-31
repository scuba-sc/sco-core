# Design: service-table lifetime audit

**Current HOLD:** `game::services::Start` refuses before native reads or
provider mutation; the previous implementation is private and unavailable.
Historical startup and rollback descriptions below do not authorize it.

Status: source audit of `0ce4ab8`, updated from `ab25aa7` for atomic administrative withdrawal, built-in provider refusal, queried failed-load retention, dependent reload cascades, restart-required fault retention and the terminal host shutdown barrier. No plugin ABI growth. This is an inventory of the current implementation, not a lease or crash-safety guarantee. See [service safety](service-safety.md) for the historical proposal and [plugins](../plugins.md#hot-reload) for the supported reload contract.

## Scope and result

A service table is the opaque pointer published through `provide_service`, not a signature-row table, VFS mount table, command-list view or IPC data mapping. The inventory covers core, game pack, SDK accessors and native provider lifecycle. Source locations are given as a repository-relative file plus the function, table or test name; line numbers are not cited because they move with every edit.

- **13 built-in named tables:** six owned by the host token and seven by the game-pack token. These are compiled-in services, not `plugins::Kind::Builtin` instances.
- **Four mid-session entry functions:** `ReloadCommand`, `app::Reload`, `UnloadOne`, `UnloadNative`. All four reach the single `UnloadGate` in `src/plugins/loader.cpp`. `CanUnload` is only an advisory inspection; `BeginUnload` atomically closes query/publication admission before any teardown. Built-in plugin owners follow the same rule as native owners.
- **Failed-load cleanup also reaches `UnloadGate`:** if a table was queried during load, the loader refuses the load and closes new admission, but retains backing state, exports, owner resources and native code. No unload export, owner release or script-runtime unload runs. Host callouts are suppressed by `ContainCallouts` for Refused entries. Shutdown, faults, explicit withdrawal and direct runtime owner release remain separate operations with the limits below.
- `app::Reload` now includes transitively loaded `requires` dependents, using resolved `Plugin::after` edges. It reserves the whole group before copies or teardown, unloads dependents before providers and reloads providers first. Busy, handed-out or non-reloadable members refuse the entire group. There is still no file watcher or dedicated C/UI reload entry; UI and C callers invoke `sco.reload`.

## Ownership and what a hand-out protects

`ProvideService` (`src/api/sco_services.cpp`) stores the provider's pointer verbatim. It copies the name, not the table or the table's backing state. `QueryService` holds the registry mutex, checks the version and records the owner in `g_handedOut` **before** returning `s.table`. A recording allocation failure returns `TooMany`, with no pointer. `ServiceExists` and `ServiceTableHandedOut` return booleans, not tables.

The hand-out mark survives `ReleaseService` and owner release. The unit is **owner**, not service or consumer: one successful lookup, even by the provider itself, makes every table of that native owner session-lifetime for administrative unload/reload. Owner handles are never reused (`host::NewPlugin`, `src/host/sco_host.cpp`). There is no release-reference API; `release_service` only withdraws a name. `ServiceRef` in the SDK is a pointer holder, not a counted reference.

`UnloadGate` (`src/plugins/loader.cpp`) applies to every plugin owner with a `self`, including `Kind::Builtin`, and explicitly refuses Faulted instances. A refusal precedes unload exports and `Release`, leaving a loaded provider intact. `KeepMapped`, used by `CloseOrKeep`, remains a separate last-line module pin after withdrawal. **A mapped DLL is not proof that heap-allocated table storage, plugin objects or game state are still alive.** Crash and shutdown consumers must not infer that a stale raw table is callable.

Registry operations are any-thread and mutex-protected. Plugin lifecycle/list mutation is a **game-thread caller contract** (`include/sco/plugins.h`); most loader functions do not independently reject a wrong-thread call. `app::Reload` does reject one (its `OnGameThread` check, `src/app/sco_app.cpp`). A lookup's thread permission never relaxes a table entry's own thread rule.

## Atomic admission and failed-load retention

`ServiceWithdrawal` (`src/api/sco_services.cpp`) holds the same mutex as `QueryService` and `ProvideService`. It inspects the whole owner group and records every withdrawing owner before releasing the lock. Administrative refusal or allocation failure leaves the whole group unchanged. A successful reservation prevents both new queries and republication (`Unavailable`, null query output), including calls synchronously inside an unload export or on a joined worker. An earlier successful query records its hand-out before the gate can run, so the gate refuses teardown instead. The mutex is never held while plugin code runs.

`CanUnload` calls `UnloadGate` in inspection mode; every actual unload rechecks using withdrawal mode. `app::Reload` uses `BeginUnload` for its entire dependent group, then `UnloadOne` reuses those reservations. Copy failure cancels reservations without teardown. An unload failure stops before that dependent's providers are touched; only still-Loaded survivors have their reservations cancelled. Released, Refused, Crashed and Faulted owners are not reopened by cancellation. Quarantine keeps any existing withdrawal reservation permanently. A still-Loaded owner whose off-thread fault verdict is queued is already released and is refused under the service-registry lock before administrative teardown.

A non-OK native/built-in load or script-runtime load uses failed-load mode: close admission even when already handed out, and refuse destructive cleanup if so. The entry stays Refused with a retention reason; cached pointers are not revoked. SDK/provider code may have already invalidated its own state inside load, independently of the host. This is a host cleanup guarantee, not a guarantee that an arbitrary failed provider remains callable.

## Built-in table census

H = `host::HostOwner()` (`src/host/sco_host.cpp`), the permanent `&g_hostOwner` token under reserved id `sco`. G = `host::GameOwner()`, permanent `&g_gameOwner` under `game`. Neither token is released; registrations and backing resources are withdrawn/stopped separately. `sco.datacore` is H-owned even though its implementation belongs to the optional game pack. Compatibility names `teleport.spatial` and `spawn.entities` are G-owned, not plugin-owned aliases.

All objects below have static table storage and host/game-pack code lifetime. Start/Stop do **not** run `CanUnload`: their owners are not unloadable native plugins. Normal `app::Start` (`src/app/sco_app.cpp`) starts these before plugins; `app::Stop` unloads plugins before stopping them. A stable address does not authorize use after Stop or across a new host session.

| Service; version | Owner; static table definition | Publishing function | Teardown and withdrawal function | Entry thread rule |
|---|---|---|---|---|
| `sco.storage`; 1.0 | H; `kTable`, `src/storage/storage.cpp` | `Start` | `Stop` | Any thread; DB calls serialized per plugin |
| `sco.settings`; 1.0 | H; `kTable`, `src/settings/settings.cpp` | `Start` | `Stop` | Any thread reads; host setters game thread |
| `sco.ui`; 1.0 | H; `kTable`, `src/ui/ui.cpp` | `Start` | `Stop` | Any thread registration; draw callbacks game thread |
| `sco.ipc`; 1.0 | H; `kTable`, `src/ipc/ipc.cpp` | `Start` | `Stop` | Any thread; channel locking |
| `sco.net`; 1.0 | H; `kTable`, `src/net/service.cpp` | `Start` | `Stop` | Any thread service calls; callbacks from game-thread Tick, never worker |
| `sco.datacore`; 1.1 | H; `kTable`, `src/datacore/service.cpp` | `Start` | `Stop` | Any thread; service mutex |
| `teleport.spatial`; 1.0 | G; `kSpatial`, `src/game/services/spatial.cpp` | `services::Start` | `services::Stop` | Game thread; 0 when not ready/wrong thread |
| `spawn.entities`; 1.2 | G; `kService`, `src/game/services/spawn.cpp` | `StartSpawn` | `StopSpawn` | Game thread; 0 on refusal |
| `game.actors`; 1.1 | G; `kActors`, `src/game/services/actors.cpp` | `StartActors` | `StopActors` | Game thread; `last_error` any thread |
| `game.vehicles`; 1.0 | G; `kService`, `src/game/services/vehicles.cpp` | `StartVehicles` | `StopVehicles` | Game thread; `last_error` any thread |
| `game.world`; 1.0 | G; `kWorld`, `src/game/services/world.cpp` | `StartWorld` | `StopWorld` | Game thread; `last_error` any thread |
| `game.entities`; 1.1 | G; `kEntities`, `src/game/services/entities.cpp` | `StartEntities` | `StopEntities` | Game thread (`alive` returns 0 off-thread); `last_error` any thread |
| `game.creative`; 1.0 | G; `kService`, `src/game/services/creative.cpp` | `StartCreative` | `StopCreative` | Game thread; `last_error` any thread |

Host service Start/Stop may be called on any thread according to their headers; the host kit serializes lifecycle on the game thread. The former game-service startup implementation in `src/game/services/spatial.cpp` rolled back siblings on failure; it is now private and unavailable. The public `services::Start` refuses before mutation on any thread. The historical teardown paths are compiled-in resource teardown, not DLL unloads. UI frame and network message pointers are callback-only, independently of table lifetime.

### Native and built-in plugin providers

`host::ProvideServiceC` (`src/host/sco_host.cpp`) publishes with the plugin's `self` owner and id prefix. `Kind::Builtin` may publish by the same route; its code never unmaps, but its unload function can destroy backing state. Direct `UnloadOne` now refuses a built-in whose table was handed out. `app::Reload` still rejects built-in targets and rejects a cascade containing a built-in dependent. No concrete product built-in table is defined here: products supply `Platform::builtins`. The loader cannot enumerate arbitrary third-party table layouts or prove provider-managed storage lifetime.

The repository's native example providers are `cpp_hello.greeter` (`sdk/examples/cpp_hello/cpp_hello.cpp`: static `kGreeter`, published from `CppHello::OnLoad`) and `cs_hello.greeter` (`sdk/examples/cs_hello/CsHello.cs`: `_greeter`, a NativeMemory table allocated once in `OnLoad` and never freed). Both publish 1.0 under their native plugin owner; their greet function is independent of per-plugin mutable state. Their lifecycle/export entry points are game-thread calls; raw service calls have no host dispatcher. Tests additionally publish synthetic `svc_provider`/`svc_prov_b/c/d.svc` (`tests/fixtures/plugins/native/fake_plugin.c`, mode 11) and stand-in tables; these are fixtures, not shipped host services. Lua exposes no `provide_service` binding (the `fns` registration list in `SetupBody`, `plugins/lua/sco_lua.c`).

## Every table-returning surface

The primary C query writes an out-pointer; cached getters return it directly. These surfaces do not themselves call `CanUnload` (which belongs to provider lifecycle).

| Function / accessor | Location | Pointer owner and lifetime; thread rule |
|---|---|---|
| `sco::QueryService` | `src/api/sco_services.cpp` | Raw provider table; hand-out tracked under registry lock, any thread |
| `host::QueryServiceC` / `sco_api.query_service` | `src/host/sco_host.cpp`, wired in `BuildApi`; member `query_service` in `include/sco_api.h` | Delegates to QueryService; same owner/rules |
| `storage::Table` | `src/storage/storage.cpp` | H static table, unconditional even when stopped; any thread |
| `settings::Table` | `src/settings/settings.cpp` | H static table, unconditional even when stopped; any thread |
| `ui::Table` | `src/ui/ui.cpp` | H static table, unconditional even when stopped; any thread |
| `ipc::Table` | `src/ipc/ipc.cpp` | H static table, unconditional even when stopped; any thread |
| `net::Table` | `src/net/service.cpp` | H static table, unconditional even when stopped; any thread |
| `datacore::service::Table` | `src/datacore/service.cpp` | H static table, unconditional even when stopped; any thread |
| `host::BuildApi` | `src/host/sco_host.cpp` | Root host `g_api`, not named service; process-static, call_once initialization; startup/game-thread contract |
| C++ `Plugin::Api` | `include/scosdk/plugin.hpp` | Borrowed root API, OnLoad through OnUnload return; no new lookup |
| C++ `ServiceRef<T>::Query` (both overloads), `Get`, `operator->` | `include/scosdk/service.hpp` | Query populates cached provider pointer; getters hand it back. Any-thread query; synchronize mutation of the wrapper itself |
| C# `Plugin.Api` | `sdk/csharp/Sco.Sdk/Plugin.cs` | Borrowed root API; cleared by SDK release |
| C# `Plugin.Query<T>`, `ServiceRef<T>.Table` | `sdk/csharp/Sco.Sdk/Services.cs` | Query outputs pointer holder; Table returns cached provider pointer, no lease; lookup any thread |
| C# `Storage.Table`, `DataCore.Table`, `DataCorePatch.T` | `sdk/csharp/Sco.Sdk/Storage.cs`; `sdk/csharp/Sco.Sdk/DataCore.cs` | Internal/private cached H pointers; underlying service thread rules |
| Lua `sco_lua_api` | `plugins/lua/sco_lua.c` | Script's borrowed root API; script/game-thread lifetime, not a service query |

The six native `Table()` getters bypass registry presence/version/hand-out checks; they cannot return an unloadable plugin's table. Their headers promise a process-lifetime address, not process-lifetime resources.

### C++ typed cached getters

All paths below are under `include/scosdk/`. Each `Open` calls `query_service`; each `Table` returns the cached pointer and inherits the service's thread/owner rules. `Open` does not confer ownership or safe use across teardown.

| Wrapper | Header | Query / raw getter |
|---|---|---|
| `Settings` | `settings.hpp` | `Open` (both overloads) / `Table` |
| `Storage` | `storage.hpp` | `Open` (both overloads) / `Table` |
| `Ui` | `ui.hpp` | `Open` (both overloads) / `Table` |
| `Ipc` | `ipc.hpp` | `Open` (both overloads) / `Table` |
| `Net` | `net.hpp` | `Open` (both overloads) / `Table` |
| `game::Actors` | `game/actors.hpp` | `Open` (both overloads) / `Table` |
| `game::Entities` | `game/entities.hpp` | `Open` (both overloads) / `Table` |
| `game::World` | `game/world.hpp` | `Open` (both overloads) / `Table` |
| `game::Vehicles` | `game/vehicles.hpp` | `Open` / `Table` |
| `game::Creative` | `game/creative.hpp` | `Open` (both overloads) / `Table` |

`DataCore::Open` (`datacore.hpp`, both overloads) retains a `ServiceRef` without a public Table getter; `DataCore::Begin` returns a patch wrapper retaining that pointer. `DataCore::AddRecordSupported` makes another query but returns a boolean. C# typed `Open` functions likewise cache Query results: `Settings.Open`, `Storage.Open`, `Ui.Open`, `DataCore.Open`, `Actors.Open`, `Entities.Open`, `World.Open`, `Vehicles.Open` and `Creative.Open` (in `Settings.cs`, `Storage.cs`, `Ui.cs`, `DataCore.cs` and `Game/*.cs` under `sdk/csharp/Sco.Sdk/`). They return status, not raw pointers. No typed C# IPC/Net layer is present.

### Lua C helpers

These native helpers return a queried pointer internally, on the script/game thread; Lua code does not receive a raw service pointer. Each operation requeries rather than holding a general service cache. The pointer belongs to H or G as in the census.

| Helper | Location |
|---|---|
| `Settings` | `plugins/lua/sco_lua.c` |
| `Storage`, `StoreCall` | `plugins/lua/sco_lua.c` |
| `UiTable` | `plugins/lua/sco_lua.c` |
| `DataCore` | `plugins/lua/sco_lua_game.c` |
| `ActorsTable` | `plugins/lua/sco_lua_game.c` |
| `Entities` | `plugins/lua/sco_lua_game.c` |
| `Vehicles` | `plugins/lua/sco_lua_game.c` |
| `Creative` | `plugins/lua/sco_lua_game.c` |
| `WorldTable` | `plugins/lua/sco_lua_game.c` |

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
| `UnloadAll`, `src/plugins/loader.cpp` | Loaded N/B/L; game thread | **Shutdown exception**, UnloadOneNow, newest first and B last; queried N stays mapped; Faulted instances are never entered |
| `QueryAndLoad`, `src/plugins/loader.cpp` | New N/B; game thread | Non-OK load -> `UnloadGate` failed-load mode; queried backing state retained; load fault -> Crash quarantine and retained Faulted state when handed out |
| `LoadNative`, `LoadBuiltin`, `src/plugins/loader.cpp` | New N/B; game thread | Ready-only load; shared QueryAndLoad cleanup. Pre-load export/info refusals have no newly published owner |
| `RefuseLoad`, `src/plugins/loader.cpp` | N/B; game thread | Clears exports and closes only after eligible cleanup; queried failed loads do not reach this sink |
| `CloseOrKeep`, `CloseModule`, `src/plugins/loader.cpp` | N | Final module pin, not a backing-state guarantee or alternative administrative unload |
| `LoadScript`, `src/plugins/loader.cpp` | L; game thread | Failure cleanup -> `UnloadGate` before Release or runtime unload; Lua cannot publish tables, but a host runtime can |
| `Crash`, `MarkCrashed`, `src/plugins/loader.cpp` | N/B/L; game thread | **Fault exception**, quarantine then Faulted with no resource teardown when handed out; otherwise Crashed and resource hooks; module retained |
| `CallPlugin`, `ContainedCallout`, `src/plugins/loader.cpp` | Callout owner or faulting N | Fault -> MarkCrashed or BlameProvider; not administrative unload |
| `BlameProvider`, `BlameTask`, `BlameProviderNow`, `src/plugins/loader.cpp` | Faulting N | Off-thread lookup/work quarantined immediately, state/reason verdict posted to game thread; quiescence/queue failure stops the process |
| `ReleaseOwner`, `ReleaseFailed`, `src/plugins/loader.cpp` | N/B/L | Internal Release helpers used after gate or at shutdown; fault quarantine is separate |
| `sco::Release`, `src/api/sco_tasks.cpp` | Runtime owner; game thread enforced | Direct runtime cleanup, not a loader administrative request; release hooks may tear down resources |
| `ReleaseService`, `detail::ReleaseServices`, `src/api/sco_services.cpp` | Published owner; any thread, lock | Name withdrawal only; hand-out mark and loader reservation are preserved |
| `host::ReleaseServiceC`, `src/host/sco_host.cpp` | Valid self; any thread | Explicit service-name withdrawal, not unload |
| `WithdrawHostService(s)`, `WithdrawGameService(s)`, `src/host/sco_host.cpp` | H/G; any thread | Registry withdrawal, not backing-state teardown |
| Host/game service Stop functions | H/G; see census | Compiled-in resource teardown, not Kind::Builtin plugin unload; unchanged |
| `Disable`, `Failed`, `sco_lua_unload`, `Close`, `plugins/lua/sco_lua.c` | L; script/game thread | Lua-owned runtime state; no native table publication binding |
| `CopyForReload`, `RemoveReloadCopies`, `src/plugins/loader.cpp` | Filesystem copies | No provider teardown or ModuleOps.close |
| Cascading reload of `requires` dependents | `app::Reload`, game thread | Entire loaded dependent closure reserved through the single loader gate; reverse teardown, forward load |
| Automatic file watcher | None implemented | Not applicable |

Resource release hooks are additional teardown sinks, not table/module owners: `OnRelease` in `src/storage/storage.cpp`, `src/settings/settings.cpp`, `src/ui/ui.cpp`, `src/ipc/ipc.cpp`, `src/net/service.cpp` and `src/datacore/service.cpp`; the game `OnRelease` in `src/game/services/spawn.cpp` delegates to `ReleaseActorsOwner` (`actors.cpp`), `ReleaseEntitiesOwner` (`entities.cpp`) and `ReleaseCreativeOwner` (`creative.cpp`). They run on the game thread through `sco::Release`; they close/despawn/reset the released consumer's resources, do not ask CanUnload and do not revoke cached tables held by other consumers.

## Shutdown

### Terminal host boundary

`app::Stop` (`src/app/sco_app.cpp`) is a game-thread operation outside an active runtime callout or reload. Wrong-thread and active-callout entries are logged and ignored, rather than tearing down a callback's live context or running inside the outer task/event/command stack. A `g_stopping` latch is set **before** `game.exit`; recursive Stop from exit/unload callbacks is a no-op. Tick and Reload also refuse while stopping. `TestRecursiveShutdown` and `TestShutdownCalloutSafety` observed the old duplicate teardown/active-callback failures before these repairs.

`game.exit` remains the last live-session notification. Its callbacks may query live services and finish synchronous cleanup. Tasks posted there are **not** promised a final execution. Once every exit callback has returned, Stop calls private `detail::StopRuntime` (`src/api/sco_tasks.cpp`) before the first native/built-in unload export or script-runtime unload:

- `detail::SetRuntimeStopping(true)` takes the service registry mutex (`src/api/sco_services.cpp`). This serializes the boundary with lookup and permanent hand-out recording in `QueryService`; a query completed on the admitted side, or a matching service returns `Unavailable` with a null output. An absent/withdrawn name still returns `NotFound`. Publication (`ProvideService`) refuses with `Unavailable`. No registry lock is held across plugin teardown.
- Task admission checks the barrier under the queue mutex (`Push`, `src/api/sco_tasks.cpp`). Shutdown then takes that same mutex, copies the bounded ring, splices the overflow list without allocation, and empties both. An in-progress admitted post is included; a later post is rejected. This includes **plugin, host-owned and ownerless work**. Drop functions run in FIFO order outside locks, freeing internal invoke/fault contexts; task functions and completion callbacks never run. Plain `Post`/`run_on_game_thread` has no drop callback: cancellation does not free arbitrary plugin-owned `ctx`, which the provider must own/clean up itself.
- `DrainTasks` (`src/api/sco_tasks.cpp`) refuses after the boundary; `detail::Callout` suppresses host as well as plugin callbacks. `Dispatch` returns `Unavailable` (`src/api/sco_events.cpp`), and `detail::InvokeOwned`/`InvokeRaw` refuse before calling plugin code (`src/api/sco_commands.cpp`, `src/api/sco_raw.cpp`). Event, command, service and raw-handler registrations cannot reopen admission during teardown. Cleanup operations such as withdrawal, unsubscribe and Release remain available.
- The barrier remains closed after Stop. The next host `app::Start` (`src/app/sco_app.cpp`) reopens admission, without replaying cancelled work. Plugin owners/hand-out marks are not recycled.

Host-owned runtime callbacks are game-thread callouts, so there is no concurrent host-dispatched callback to wait for at the accepted Stop entry. Queued host-owned callbacks are cancelled before plugin state is freed. This is **not** a join of arbitrary provider worker threads, an interruption of direct raw-table calls, or a safety guarantee for custom resource hooks. Providers must stop/join their own workers before teardown, normally during `game.exit`; native calls outside host guards remain uncontained on POSIX.

### Teardown and peer calls

`UnloadAll` (`src/plugins/loader.cpp`) preserves newest-first ordering with built-ins last; it does not refuse healthy Loaded providers just because a table was handed out. The native/built-in sink `UnloadCode` runs the unload export, then Release, then close-or-pin. The script sink `UnloadScript` releases host registrations/resources before runtime unload. Host/game service Stops follow plugin teardown in `app::Stop` (`src/app/sco_app.cpp`). A failed Release keeps the module/runtime and reports failure rather than unmapping code; the terminal barrier still suppresses residual host callbacks.

Faulted instances are never entered. An owner already quiesced by an off-thread fault but still awaiting its game-thread verdict also stays retained: `UnloadOneNow` (`src/plugins/loader.cpp`) converts it to terminal Faulted without unload/resource hooks. Queued attribution records now carry `DropBlame` as their drop function (posted by `BlameProvider`) so terminal cancellation deletes their internal context. `TestShutdownPendingFault` failed before this guard; `TestPendingFaultShutdown` exercises real joined-worker attribution and its actual queued record.

A shutdown unload callback may use its own still-live state and cleanup APIs. It must **not** acquire new service tables, post work, dispatch events or invoke peer commands; those paths are closed. A cached peer-provider table is not a lease: actual queried dependencies are not necessarily declared `requires`, and built-ins-last/reverse load order is not proof that the peer's backing state still exists. Do not call cached plugin-provider entries from unload callbacks or after Stop. A provider must define any stronger, explicitly coordinated teardown contract itself. Host static table addresses may still exist, but their resource lifetime remains separate.

`UnloadAll` alone is a lower-level loader teardown primitive, **not** a complete host-session shutdown: it does not itself send `game.exit`, close global runtime admission or cancel host-owned work. Products use `app::Stop` for that boundary. Direct loader unit cases exercise teardown/refusal/module pinning separately and must not be cited as proof of the host-session drain.

### Lifecycle coverage matrix

CTest registers whole executables, not each C++ test function. **Historical census (2026-10-11, audit of `0ce4ab8`):** `ctest -N` enumerated 34 kernel tests before the new shutdown gate; the audit then recorded **35** kernel tests and **43** with the game pack. The following list preserves that historical inventory, not the current kernel/game split; game ABI pins are now conditional on `SCO_GAME_SC`. For the current configured inventory, use `ctest --test-dir <build> -N` with `SCO_BUILD_TESTS=ON` and state the `SCO_GAME_SC` setting and Python availability. Registrations in [`CMakeLists.txt`](../../CMakeLists.txt) have since changed, including language-route and SDK conformance coverage.

```text
abi_v1 abi_freeze abi_storage abi_settings abi_game_vehicles abi_game_entities
abi_game_creative abi_ui abi_datacore abi_ipc abi_sc_net abi_net abi_titanlink
abi_voxel_bridge abi_game_world abi_game_events abi_game_actors
 test_core test_runtime test_host test_sdk test_storage test_settings test_ipc
 test_net test_net_service test_ui test_hook test_spatial test_vfs test_plugins
 test_lua test_app test_shutdown_drain host_sim_examples
```

In that historical census the game-pack configuration added `test_death_events`, `test_pak`, `test_datacore`, `test_datacore_patch`, `test_datacore_pack`, `dcb_pack`, `test_datacore_service` and `dcb_tool`. `test_shutdown_drain` executes only the shutdown cases; `tools/test.sh` runs the same dedicated executable mode.

| Requested row | Existing evidence | Gap filled / limits |
|---|---|---|
| Cached tables | `TestServices`, `tests/test_plugins.cpp`; SDK `ServiceRef` cases in `main`, `tests/test_sdk.cpp` | Both public unload refusals preserve table identity/code; fault and shutdown module pinning covered. No claim of callable arbitrary state after terminal teardown. |
| Denied provider reload | `TestReload` and `TestReloadCascade`, `tests/test_app.cpp`; `TestFaultedProviderUnloadRefusal`, `tests/test_plugins.cpp` | App, command, C invocation and UI paths; whole-group refusal/rollback. Already covered; no duplicate row test. |
| Recursive callbacks | Nested drain/tick in `TestTaskQueue`, `tests/test_runtime.cpp`; nested provider/unload faults in `TestServices`/`TestNative`; `TestDoneAfterRelease`, `tests/test_runtime.cpp` | **New:** `TestRecursiveShutdown` and `TestShutdownCalloutSafety`, `tests/test_app.cpp`, cover recursive exit/unload, off-thread Stop and Stop from an active event. |
| Failed release | The wrong-thread Release case in `TestNative`, `tests/test_plugins.cpp` | Wrong-thread Release keeps code mapped and suppresses callbacks. Allocation-failure injection is not covered; do not equate this test with exhaustive OOM behavior. |
| Pending tasks at unload | Owner Release ring/overflow in `TestTaskQueue`; queued commands in `TestCommands`, `tests/test_runtime.cpp`; host API release in `TestTable`, `tests/test_host.cpp` | **New:** `TestShutdownDrain` and `TestShutdownQueueOverflow`, `tests/test_app.cpp`: terminal ring/overflow cancellation, owned cleanup exactly once/FIFO, host/null/plugin owners, real off-thread C invoke/copied strings, no completion/task replay, synchronous and joined-worker lookup/post refusal during unload. |
| Provider fault | `TestServices`, `TestFaultedProviderUnloadRefusal`, `TestQuarantinedUnloadReservation`, `tests/test_plugins.cpp`; `TestReload`, `tests/test_app.cpp` | **New:** `TestShutdownPendingFault`, `tests/test_app.cpp`, and real native `TestPendingFaultShutdown`, `tests/test_plugins.cpp`: pending quiesced verdict cannot re-enter unload or release backing state. POSIX tests report a synthetic module trace; Windows CI exercises its guard separately. |
| Repeated shutdown | `TestApp`, `tests/test_app.cpp`, plus storage/settings/net Stop cases | Sequential no-op already covered; new recursive cases verify exactly one exit and one unload. |
| Missing capability | `TestDiscover` and `TestRequiresOrder`, `tests/test_plugins.cpp`; `TestCommands`, `tests/test_runtime.cpp`; `TestApp` | Discovery/load and gated command refusal already covered; no extra test needed. |

Each production defect repair was preceded by the narrow shutdown gate failing for the expected reason. Additional coverage-only cases exercise already-repaired cancellation branches without inventing another defect. Cached-pointer safety after arbitrary provider-managed destruction, uncontrolled worker faults and allocation-failure permutations remain outside this evidence.

### Local verification limits

AppleClang supports the requested C++ `-fsanitize=address,undefined` build. Kernel `test_runtime`, `test_plugins`, `test_app` and `test_shutdown_drain` pass with ASan/UBSan (`UBSAN_OPTIONS=halt_on_error=1`); fake native C modules and vendor C code are uninstrumented in this C++-flags configuration. An additional attempt instrumenting C too encounters macOS dynamic-fixture ODR/global-registration failures; it is not counted as a passing fully instrumented run.

The requested full kernel and game-pack builds were attempted on macOS, but stop in pre-existing VFS libc++ `atomic<shared_ptr>` errors and warning-as-error failures in VFS/hooks; the game-pack app target also depends on those files. The kernel lifecycle/ABI-freeze targets build and pass independently. This local result does not substitute for full Linux/Windows CI, the Windows SEH stack walk, or running game-service Stop inside a game. Not run in game.

## Regression evidence and limits

SDK provider-side teardown is reached from the guarded unload export, not through a second host predicate: C++ `detail::Slot<T>::Unload` (`include/scosdk/plugin.hpp`) calls `UnloadPlugin`, then deletes the plugin object; `Plugin::Shutdown` clears SDK callback state, and failed `LoadPlugin` also calls it. `SCO_PLUGIN` emits the `sco_plugin_unload` export. C# `PluginExports.Unload` (`sdk/csharp/Sco.Sdk/Plugin.cs`) calls `UnloadPlugin`, then `Plugin.Release` clears callbacks/arena/API; failed `LoadPlugin` also calls Release. These game-thread provider hooks do not consult CanUnload themselves, and a provider can still invalidate its own heap-backed table/state outside host lifecycle. They are not extra administrative host unload routes.

The standalone checker is a different one-plugin host. Its `query_service` (`sdk/tools/sco-plugin-check.c`) records every queried name and hands out exactly one table: the checker's own static `sco.widgets` stand-in (`widget_api`), when the name is `sco.widgets`, the headless widgets service is still running and the requested minimum version is a 1.x no newer than 1.0. Any other name, or `sco.widgets` after the widgets stand-in has stopped, gets NotFound with a null out-pointer; a different major or newer minor gets Unavailable; a null name or out-pointer gets BadArg. `provide_service` only lists a plugin's own tables and never connects them to anyone, so the checker cannot hand out a provider table, and `release_service` only withdraws a name the plugin itself provided. Its `check_native` shutdown calls the plugin's `sco_plugin_unload` export and intentionally leaves the module mapped. `tools/sco-host-sim.cpp` instead shuts down through `app::Stop` at the end of `main`. Neither supplies a hidden live-session reload path.

`tests/test_plugins.cpp`: `TestServices` covers real-DLL refusal through both public unload routes and terminal module pinning. `TestAtomicUnloadAdmission` deterministically queries and attempts republication inside the unload export, with an additional joined worker query (no sleeps). `TestBuiltinServiceUnloadRefusal` proves unload exports and release hooks cannot destroy queried built-in backing state, even after explicit withdrawal/republication. `TestFailedLoadServiceRetention` covers native and built-in non-OK loads; `TestFailedScriptServiceRetention` covers a publishing host script runtime. `TestFaultedProviderUnloadRefusal` proves a handed-out faulted native is refused by the shared unload/reload gate without exports, resource hooks or module teardown. `TestQuarantinedUnloadReservation` proves an existing reservation remains terminal across quarantine/cancellation before the queued state verdict. Each new path was observed failing before its fix. `tests/test_app.cpp`: `TestReload` covers app, command, C API invoke and UI dispatch refusal; `TestReloadCascade` covers transitive/diamond ordering, whole-group reservation, copy rollback, busy and handed-out dependents, and partial-failure behavior; `TestReloadNonCodeDependents` covers built-in/data cascade refusal.

Local verification uses the kernel-only CMake `test_plugins` and `test_app` targets. That covers the refusal logic and real host shared-library loading, not the Windows SEH stack walk or game-pack Stop behavior in a running game. Not run in game. The full local `tools/test.sh` path is not used because of the known unrelated macOS warning-as-error failures in the VFS/pak code.

## Open: provider withdrawal, raw calls and caches

The atomic administrative race, built-in plugin refusal and queried failed-load cleanup findings are repaired above. Fault behavior now follows the retention policy below; the other limitations remain:

- **Explicit withdrawal/republication:** release_service withdraws a name, not a reference. Outside a loader reservation, a provider may subsequently free/replace dynamic table memory while existing consumers retain it. The host gate cannot constrain arbitrary provider-managed teardown or replacement.
- **Fault behavior (implemented after this audit):** the common `Crash` path quiesces the owner before testing the permanent hand-out mark: new host registrations are rejected and lookup/scheduling withdrawn. A handed-out provider becomes internal `State::Faulted`, retains its state/module, and never runs unload or resource release hooks, even through `UnloadAll`. Logs, status, plugin reports and reload refusal instruct the user to restart the game. A guarded off-thread provider verdict quiesces lookup/scheduling immediately without freeing resources and queues only the game-thread state/reason update; it remains restart-required even without a recorded hand-out. Quiescence/queue failure causes a logged controlled process stop. This is partial attribution, not a process security boundary or recovery from corrupted native state. Raw cached tables are not revoked or made safe; already-running calls are not interrupted. Off-thread faults outside a host guard are not contained; POSIX's default guard contains nothing.
- **Shutdown ordering:** the terminal admission/cancellation barrier above is implemented; reverse declared load order and built-ins-last remain unchanged and are not a graph of all actual queried dependencies. Stop is terminal for consumers; raw tables confer no teardown lease. Crash/killed-process shutdown is best effort.
- **Static getters and SDK caches:** native Table() getters return a non-null static address even after Stop. SDK copies do not automatically invalidate on withdrawal/fault/Stop. C# typed Open methods can retain the old pointer on failed reopen; lookup-again is not a lease either. Do not strengthen this into an all-functions-return-Unavailable claim: UI chord normalization is pure, DataCore state is a state value, and Net predicates can return 0.

`sco_api.h`, its public enums, service table layouts, relative plugin teardown ordering and SDK caches are unchanged. Terminal host admission/cancellation and shutdown reentrancy are repaired above. Fault handling changes only host-internal state and implementation; dependent reload policy changes only as described above. Raw table references are never revoked. Current fault code is in `Crash`/`BlameProvider` and private runtime `QuarantineOwner`.
