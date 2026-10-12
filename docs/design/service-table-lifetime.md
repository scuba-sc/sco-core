# Design: service-table lifetime audit

Status: source audit of `0ce4ab8`, with the `UnloadNative` refusal bypass repaired in this change. No plugin ABI growth. This is an inventory of the current implementation, not a lease or crash-safety guarantee. See [service safety](service-safety.md) for the historical proposal and [plugins](../plugins.md#hot-reload) for the supported reload contract.

## Scope and result

A service table is the opaque pointer published through `provide_service`, not a signature-row table, VFS mount table, command-list view or IPC data mapping. The inventory covers core, game pack, SDK accessors and native provider lifecycle. Source locations below are repository-relative definition/registration lines in the audited tree; loader lines are unchanged by the one-line routing fix.

- **13 built-in named tables:** six owned by the host token and seven by the game-pack token. These are compiled-in services, not `plugins::Kind::Builtin` instances.
- **Four implemented mid-session unload/reload entry functions:** `ReloadCommand`, `app::Reload`, `UnloadOne`, `UnloadNative`. All four now reach `CanUnload`; the first two through the reload path, the last two through `UnloadOne`. Previously, `UnloadNative` called `UnloadCode` directly and tore down a handed-out provider's state despite keeping its module mapped.
- Shutdown, failed-load cleanup, faults, explicit service withdrawal and owner release are **different operations**, not silently counted as guarded unload requests. Their exceptions and remaining risks are listed below.
- There is no dependent-cascading reload, automatic file watcher or dedicated C/UI reload entry in this repository. `requires` controls startup ordering/checks, not cascade teardown. UI and C callers can invoke the named `sco.reload` command.

## Ownership and what a hand-out protects

`ProvideService` (`src/api/sco_services.cpp:42`) stores the provider's pointer verbatim. It copies the name, not the table or the table's backing state. `QueryService` (`:61`) holds the registry mutex, checks the version and records the owner in `g_handedOut` **before** returning `s.table`. A recording allocation failure returns `TooMany`, with no pointer. `ServiceExists` (`:82`) and `ServiceTableHandedOut` (`:90`) return booleans, not tables.

The hand-out mark survives `ReleaseService` and owner release. The unit is **owner**, not service or consumer: one successful lookup, even by the provider itself, makes every table of that native owner session-lifetime for administrative unload/reload. Owner handles are never reused (`host::NewPlugin`, `src/host/sco_host.cpp:296`). There is no release-reference API; `release_service` only withdraws a name. `ServiceRef` in the SDK is a pointer holder, not a counted reference.

`CanUnload` (`src/plugins/loader.cpp:211`) consults this mark only for `Kind::Native`. A refusal precedes `sco_plugin_unload` and `Release`, leaving the provider Loaded and intact. `KeepMapped` (`:198`), used by `CloseOrKeep` (`:221`), is a separate last-line module pin after withdrawal. **A mapped DLL is not proof that heap-allocated table storage, plugin objects or game state are still alive.** Crash and shutdown consumers must not infer that a stale raw table is callable.

Registry operations are any-thread and mutex-protected. Plugin lifecycle/list mutation is a **game-thread caller contract** (`include/sco/plugins.h`); most loader functions do not independently reject a wrong-thread call. `app::Reload` does reject one (`src/app/sco_app.cpp:235`). A lookup's thread permission never relaxes a table entry's own thread rule.

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

`host::ProvideServiceC` (`src/host/sco_host.cpp:223`) publishes with the plugin's `self` owner and id prefix. `Kind::Builtin` may publish by the same route; its code never unmaps, but its unload function can destroy backing state. `app::Reload` rejects built-ins; a direct `UnloadOne` accepts them because `CanUnload` only protects natives. No concrete product built-in table is defined here: products supply `Platform::builtins`. The loader cannot enumerate arbitrary third-party table layouts or prove their storage lifetime.

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

| Function/path; location | Owner; thread rule | Routes through `CanUnload`? / effect |
|---|---|---|
| `ReloadCommand`, `src/app/sco_app.cpp:46` | Target N/L; command runs game thread | **Yes**, via `Reload` |
| `app::Reload`, `src/app/sco_app.cpp:230` | Target N/L; game thread enforced | **Yes**, :245 before copy/unload; `UnloadOne` checks again. B/data rejected |
| `UnloadNative`, `src/plugins/loader.cpp:444` | N; game-thread contract | **Yes after this fix**, via `UnloadOne`; previously bypassed it |
| `UnloadOne`, `src/plugins/loader.cpp:528` | N/B/L; game-thread contract | **Yes**, :531 before teardown; B/L/data pass predicate |
| C API `invoke` / `host::InvokeC`, `src/host/sco_host.cpp:189` | Command caller; any thread, off-thread queued | **Indirectly**, when name is `sco.reload`; no C unload/reload function exists |
| UI hotkey `ui::Dispatch`, `src/ui/ui.cpp:617` | Hotkey binder; invokes command, off-thread queued | **Indirectly**, when bound name is `sco.reload`; UI has no provider teardown entry |
| `UnloadOneNow`, `src/plugins/loader.cpp:518` | N/B/L; game thread | No local check; private sink, called only by checked `UnloadOne` or shutdown `UnloadAll` |
| `UnloadCode`, `src/plugins/loader.cpp:431` | N/B; game thread | No local check; guarded unload export, ReleaseOwner, CloseOrKeep. Only reachable through preceding sink |
| `UnloadScript`, `src/plugins/loader.cpp:509` | L; game thread | No local check; Release before runtime unload; Lua cannot publish native service tables |
| `app::Stop`, `src/app/sco_app.cpp:206` | All plugin owners, then H/G; game-thread contract | **Shutdown exception**; game.exit, UnloadAll, service Stops and residual withdrawals |
| `UnloadAll`, `src/plugins/loader.cpp:564` | All Loaded N/B/L; game thread | **Shutdown exception**, calls UnloadOneNow directly, newest first and B last; handed-out N stays mapped |
| `QueryAndLoad`, `src/plugins/loader.cpp:239` | New N/B; game thread | **Failed-load exception**; load fault -> Crash; non-OK result -> ReleaseOwner then RefuseLoad |
| `LoadNative`, `src/plugins/loader.cpp:284` | New N; game thread | No unload refusal; Ready-only open, missing export -> RefuseLoad; not a live-provider reload entry |
| `LoadBuiltin`, `src/plugins/loader.cpp:338` | New B; game thread | No unload refusal; Ready-only QueryAndLoad, no module |
| `RefuseLoad`, `src/plugins/loader.cpp:228` | N/B; game thread | No check; CloseOrKeep then clear exports/state Refused; failed load, not administrative unload |
| `CloseOrKeep`, `src/plugins/loader.cpp:221`; `CloseModule`, :54 / :62 | N; game thread | No refusal; **pin** via KeepMapped before ModuleOps.close -> FreeLibrary/dlclose |
| `LoadScript` refusal/error cleanup, `src/plugins/loader.cpp:465` | L; game thread | No native provider; ReleaseOwner, runtime unload on non-OK script load |
| `Crash`, `src/plugins/loader.cpp:135` | N/B/L; game thread | **Fault exception**, ReleaseOwner and Crashed; never closes module |
| `MarkCrashed`, `src/plugins/loader.cpp:345` | Loaded N/B/L; game thread | No refusal; delegates to Crash, cannot leave faulting plugin enabled merely because it provided a table |
| `CallPlugin`, `ContainedCallout`, `src/plugins/loader.cpp:399,413` | Callout owner or faulting N provider | No refusal; CallPlugin faults -> MarkCrashed or BlameProvider; lifecycle game-thread contract |
| `BlameProvider`, `BlameTask`, `BlameProviderNow`, `src/plugins/loader.cpp:389,382,370` | Faulting N provider | No refusal; off-thread provider verdict posted to game thread before MarkCrashed; queue failure leaves it enabled and logs |
| `ReleaseOwner`, `ReleaseFailed`, `src/plugins/loader.cpp:117,127` | N/B/L; Release requires game thread | No refusal; runtime Release, or Crashed/kept loaded on release failure |
| `sco::Release`, `src/api/sco_tasks.cpp:218` | Any runtime owner; game thread enforced | No loader predicate; removes services/callbacks/tasks and runs resource release hooks, never closes DLL |
| `ReleaseService`, `detail::ReleaseServices`, `src/api/sco_services.cpp:95,106` | Published owner; any thread, registry lock | No refusal; withdraws names only, hand-out mark retained; no provider teardown itself |
| `host::ReleaseServiceC`, `src/host/sco_host.cpp:232` | Valid plugin self; any thread | No refusal; explicit C service withdrawal, not native unload |
| `WithdrawHostService(s)`, `src/host/sco_host.cpp:324,326` | H; any thread | No predicate; named/all registry withdrawal; no module teardown |
| `WithdrawGameService(s)`, `src/host/sco_host.cpp:347,349` | G; any thread | No predicate; named/all registry withdrawal; no module teardown |
| Host and game service Stop functions | H/G; see census | No predicate; resource teardown and withdrawal; normal app path is after plugin unload; startup rollback also uses game Stops |
| `Disable`, `Failed`, `plugins/lua/sco_lua.c:199,217` | L; script/game thread | No provider table; alive=0, unsubscribes script events; state not freed here |
| `sco_lua_unload`, `Close`, `plugins/lua/sco_lua.c:1314,1273` | L; game thread, no active script depth | No provider table; frees script state; load-error cleanup also calls Close |
| `CopyForReload`, `RemoveReloadCopies`, `src/plugins/loader.cpp:542,553` | Native filesystem copies; lifecycle game thread | No provider teardown; copies/removes paths, never calls ModuleOps.close |
| Cascading reload of `requires` dependents / watcher | None implemented | Not applicable; `UnmetRequires` in Reload checks target's dependencies, not consumers |

Resource release hooks are additional teardown sinks, not table/module owners: `OnRelease` in `src/storage/storage.cpp:772`, `src/settings/settings.cpp:237`, `src/ui/ui.cpp:440`, `src/ipc/ipc.cpp:372`, `src/net/service.cpp:613`, `src/datacore/service.cpp:390`; game `spawn.cpp:263` delegates to `ReleaseActorsOwner` (`actors.cpp:649`), `ReleaseEntitiesOwner` (`entities.cpp:1095`), `ReleaseCreativeOwner` (`creative.cpp:666`). They run on the game thread through `sco::Release`; they close/despawn/reset the released consumer's resources, do not ask CanUnload and do not revoke cached tables held by other consumers.

## Regression evidence and limits

SDK provider-side teardown is reached from the guarded unload export, not through a second host predicate: C++ `detail::Slot<T>::Unload` (`include/scosdk/plugin.hpp:447`) calls `UnloadPlugin` (:695), then deletes the plugin object; `Plugin::Shutdown` (:657) clears SDK callback state, and failed `LoadPlugin` (:671) also calls it. `SCO_PLUGIN` emits the export at :724. C# `PluginExports.Unload` (`sdk/csharp/Sco.Sdk/Plugin.cs:446`) calls `UnloadPlugin` (:474), then `Plugin.Release` (:335) clears callbacks/arena/API; failed `LoadPlugin` (:458) also calls Release. These game-thread provider hooks do not consult CanUnload themselves, and a provider can still invalidate its own heap-backed table/state outside host lifecycle. They are not extra administrative host unload routes.

The standalone checker is a different one-plugin host: `sdk/tools/sco-plugin-check.c:891` always sets its query out-pointer null and returns NotFound/BadArg. It cannot hand out a provider table; its `check_native` shutdown calls the unload export at :1074 and intentionally leaves the module mapped. `tools/sco-host-sim.cpp:234` instead shuts down through `app::Stop`. Neither supplies a hidden live-session reload path.

`tests/test_plugins.cpp`, `TestServices`, now checks both public single-plugin unload routes with a real provider DLL and held table: state/module/self unchanged, unload counter zero, no close, same table still resolving and callable. A plain native still unloads/closes. `UnloadAll` still runs the provider's unload once, withdraws its name and keeps its module mapped. The changed test failed against the original direct `UnloadNative -> UnloadCode` implementation, then passed after routing through `UnloadOne`. `tests/test_app.cpp`, `TestReload`, already checks app reload and `sco.reload` refusal, copy/close/unload counters, consumer behavior and shutdown.

Local verification uses the kernel-only CMake `test_plugins` and `test_app` targets. That covers the refusal logic and real host shared-library loading, not the Windows SEH stack walk or game-pack Stop behavior in a running game. Not run in game. The full local `tools/test.sh` path is not used because of the known unrelated macOS warning-as-error failures in the VFS/pak code.

## Open: lifecycle centralization and fault behavior

These findings are recorded, not changed by this audit:

- **Concurrent lookup versus administrative unload:** `CanUnload` releases the registry lock before unload runs. An any-thread query can first hand out a table after that check (or synchronously inside the unload export) and before Release withdraws it. CloseOrKeep will pin the code afterward, but state teardown has already happened. There is no atomic admission/teardown gate; checked routing alone is not a proof against this race.
- **Explicit withdrawal/republication:** release_service withdraws a name, not a reference. A provider may subsequently free/replace dynamic table memory while existing consumers retain it. Built-ins pass CanUnload and their state can also be torn down without unmapping code. Centralizing all lifecycle/withdrawal rules needs its own change.
- **Fault and failed-load behavior:** Crash releases provider-owned resources despite handed-out tables and keeps code mapped. A plugin can publish and have its table queried during load, then fail loading; cleanup still releases it. There is no provider-call wrapper, lease, availability guard or stale-state guarantee. Off-thread calls outside a host guard are not contained; POSIX's default guard contains nothing. The queued provider-blame path differs from owner blame and is not general thread-safe lifecycle permission.
- **Shutdown ordering:** reverse declared load order and built-ins-last is not a graph of all actual queried dependencies. A consumer without a declared requires may unload after a provider. Stop is terminal for consumers; do not keep calling raw tables during/after shutdown based only on module pinning. Crash/killed-process shutdown is best effort.
- **Static getters and SDK caches:** native Table() getters return a non-null static address even after Stop. SDK copies do not automatically invalidate on withdrawal/fault/Stop. C# typed Open methods can retain the old pointer on failed reopen; lookup-again is not a lease either. Do not strengthen this into an all-functions-return-Unavailable claim: UI chord normalization is pure, DataCore state is a state value, and Net predicates can return 0.

None of those findings changes `sco_api.h`, service table layouts, crash policy or dependency-reload policy in this change.
