# Plugins: discovery, loading and data packs

`sco/plugins.h`, `src/plugins/`. Internal C++ used by the host (sc-offline); plugins themselves only see [`sco_api.h`](api-v1.md).

## The folder

Every plugin is a folder `data/plugins/<id>/` with a `plugin.ini`:

```ini
id = hello              ; [a-z0-9_], 1-31 chars, same as the folder name; also the command prefix
name = Hello            ; shown in the menu and status
version = 1.0.0
author = you            ; optional
api = 1.0               ; sco_api major.minor it needs
kind = native           ; native | lua | data
entry = hello.dll       ; native: the DLL; lua: the main script; data: none
requires = teleport, spawn.ship   ; optional: capabilities, services, plugin ids (see Load order)
```

- `;` or `#` starts a comment at the start of a line or after whitespace. CRLF and a UTF-8 BOM are fine. Unknown keys are ignored, so a later minor can add some.
- Reserved ids: `sco`, `host`, `menu`, `game`.
- `entry` is a bare file name inside the plugin folder: no `/`, `\`, `:` or `..`.
- `plugin.ini` is at most 16 KiB.
- A line `[name]` starts a section that runs to the next one. Only `[settings]` is defined ([typed settings](#typed-settings-settings)); keys go above it. Other sections are skipped like unknown keys.

## Discovery

`Discover(root, options)` lists every subfolder of `root` that holds a `plugin.ini`, in byte order of the folder name. It never runs plugin code and never opens the entry file. Symlinked folders are skipped. Each plugin ends in one state:

| State | When |
|---|---|
| `off` | `plugins = off` in `sc-offline.ini` (the default). Listed so `status` can show it |
| `disabled` | `data/plugins/<id>/disabled` exists (a file or a folder) |
| `refused: <reason>` | `the folder name 'sco' is reserved for the host` (`sco` is the host's own id, under which it publishes [host-owned services](api-v1.md#host-owned-services); `host`, `menu` and `game` likewise; checked first, even for a disabled folder), `plugin.ini: <parse error>`, `id 'x' does not match folder 'y'`, `id 'x' belongs to a built-in plugin` (a folder whose id is one of the host's built-ins, whatever its kind; checked before the api and entry checks, so a folder that may not load at all never has its entry opened; a disabled folder stays `disabled` and appends the same reason to `disabled file`), `built for api M.m` (major differs or minor newer than the host), `entry 'x' not found`, `missing capability 'x'`, `requires service x.y: no plugin provides it`, `requires cycle: a -> b -> a`, `requires b: plugin 'b' is refused (<reason>)` (see [Load order](#load-order-and-requires)), `too many plugins` (over 128) |
| `ready` | Passed; the loader, the Lua runtime or the content index takes it |

## Load order and requires

`requires` is a comma list (at most 16, no repeats, spaces ignored) whose names `Discover` resolves, after every folder is listed, in this order:

1. **The plugin itself** (`a` or `a.x` in `a`'s own list) is a cycle, refused as `requires cycle: a -> a`. A capability or published service with the same name wins over this check.
2. **A capability or a service the host or the game pack already published** (`opts.has`: `sco.storage`, `sco.ui`, `game.vehicles`, `teleport.spatial`, `teleport`) is provided: no ordering needed.
3. **A built-in's id** (`core1` or `core1.cmd`): provided, because built-ins load first.
4. **A discovered plugin's id** (`b`) **or one of its services** (`b.svc`): the plugin loads after `b`. `b` is any native, Lua or data folder; its id is the part before the first `.`. The reserved ids (`sco`, `host`, `menu`, `game`) never match a folder.
5. **Anything else is missing:** `missing capability 'x'` for a name with no dot, `requires service x.y: no plugin provides it` for a dotted one (a capability the game build doesn't have reads the same way).

**Why there is no `provides` key.** A plugin can only publish a service named `<its id>` or `<its id>.<name>` (`ProvideService` refuses any other), so the id before the dot already names the provider, before any code runs. Ordering therefore needs nothing beyond `requires`. The cost: the host orders and checks by plugin id, it doesn't know whether the provider will really publish `b.svc` (it may publish it later, or not at all). A caller still checks `query_service`'s result.

**Order.** Built-ins first, then the discovered plugins by a stable topological sort (`LoadOrder`): the free plugin with the lowest folder index goes next, so folders keep byte order wherever nothing requires otherwise. `aaa` requiring `zzz.svc` loads after `zzz`, and the plugins in between keep their places. `UnloadAll` runs in reverse load order, so a provider unloads after the plugins that needed it. The list itself stays in folder order; the load order is `Plugin::loadOrder`.

**Refusals** (all before any plugin code runs, one `[plugin] refused <id>: <reason>` line each):

| Reason | When |
|---|---|
| `requires cycle: a -> b -> a` | Every plugin on a cycle (the member that sorts first leads; a self-require is `a -> a`) |
| `requires b: plugin 'b' is refused (<reason>)` | The provider is refused, disabled, crashed or didn't load. It chains: a plugin requiring that one is refused the same way |
| `requires service x.y: no plugin provides it` | No capability, service, built-in or plugin id matches |

The same check runs again right before each plugin loads (`UnmetRequires`): the provider must be `Loaded` (a data pack: `Ready`, as it is indexed after the loads), so a provider whose `sco_plugin_load` fails or whose script errors refuses its dependents instead of starting them. A native, Lua and data plugin all follow the same rules.

## Native plugins

`LoadNative(plugin, api, self, options)`, on the game thread:

1. `LoadLibraryExW(entry, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)`: the plugin's own dependencies come from its folder or System32, never the game folder or `PATH`.
2. Resolves `sco_plugin_query`, `sco_plugin_load` and `sco_plugin_unload`; any missing refuses the plugin.
3. `sco_plugin_query()` must return info whose `size` covers `author`, with `api_major` equal to the host's, `api_minor` not newer, and `name` equal to the id.
4. `sco_plugin_load(api, self)`; anything but `SCO_OK` refuses the plugin. Cleanup atomically closes query/publication admission before release. If a table was handed out during load, backing state, exports and code are retained instead; host-managed callouts are suppressed.

`self` is the host's owner handle for the plugin; the host never reuses one. A refusal closes the DLL only when no table was handed out and owner release succeeded.

On Windows `LoadLibraryExW` runs under `SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)`: a file that isn't a valid 64-bit PE (a broken download, a 32-bit DLL) is refused with a reason instead of raising Windows' modal "Bad Image" box over the game. Loading never shows UI.

### Crash containment

Native plugins are trusted in-process code, not sandboxed extensions. The supported thread, cached-table, unmanaged-worker and shutdown limits are in [Plugin rules](../sdk/docs/plugin-rules.md#native-trust-and-supported-usage).

Host-entered plugin calls go through `Guarded()`: on Windows a `__try/__except` boundary for SEH faults, not a guarantee of safe continuation. `query`, `load` and `unload` are guarded by the loader itself. Registered callbacks/tasks/commands and `invoke` completion callbacks use the runtime guard installed by the host. Custom unmanaged threads and direct raw service calls are not automatically guarded. The host installs the runtime callout guard once, right after `Discover`:

```cpp
auto list = sco::plugins::Discover(root, opts);
sco::plugins::ContainCallouts(&list);   // every plugin callout -> CallPlugin(plugin, where, ...)
```

`ContainCallouts` (over `sco::SetCalloutGuard`) finds the plugin whose `self` owns the callout and runs it through `CallPlugin(plugin, where, thunk, ctx)`, where `where` is the event name (`tick`), the command name, `task` or `invoke done`. Host features' own callouts, and calls a plugin makes into itself while its `sco_plugin_load` is still running (already inside the load guard), are called directly. A fault:

- Lookup and scheduling are withdrawn first: the owner cannot publish new services or register new host work; its services, subscriptions, commands, raw handlers and queued work are removed. Host-managed callouts reject the owner, including callbacks already present in a dispatch snapshot. Already-running calls are not interrupted.
- If a service table of the provider was handed out, the plugin becomes **`faulted` / restart-required**. The host does **not** call `sco_plugin_unload`, `Release(self)` or the resource release hooks; provider state and its module are retained. This applies to built-in providers too (their code is already mapped). The state is terminal for the instance, including at `UnloadAll`; restart the game, do not attempt a reload.
- Without a handed-out table, host resource hooks still run and the plugin becomes `crashed`; it is never called again. This cleanup is not proof that the native process is healthy; restart after a native fault.
- a command that faulted answers `SCO_CRASHED` (with an empty reply) to whoever invoked it;
- `mod.log`, the status message and `Describe`/`LogReport` expose a faulted provider with `restart the game`; `sco.reload` refuses it with the same instruction. A non-provider still logs `[plugin] hello crashed in tick (0xC0000005) and was disabled`;
- the DLL stays mapped, because its code may still be on a stack.

**Who is blamed.** The guard's filter records which module each frame of the faulting thread was in (Windows, x64: `RtlLookupFunctionEntry` + `RtlVirtualUnwind` from the exception's context, at most 32 frames, up to the guard's own frame; `GetModuleHandleExW` with `FROM_ADDRESS | UNCHANGED_REFCOUNT` per instruction pointer). `CallPlugin` takes the first frame, innermost first, that lies in a plugin's DLL:

- it is the callout's owner, or there is none (the fault is in game or host code), or the owner is a built-in (built-ins share the product's module, so a fault in one is a product bug and keeps owner blame): the owner is marked crashed, as above;
- it is another plugin, typically a provider whose service the owner called through `query_service`: that provider is quiesced as above and the owner stays loaded. Its callout did not complete, so a command still answers `SCO_CRASHED`; keeping the caller loaded does **not** certify its state or the process as healthy. A provider already faulted, crashed or unloaded (its DLL is kept mapped) is only logged (`faulted again in a service call from ...`). For an attributed fault caught inside an off-thread host guard, lookup and host scheduling are quiesced immediately without resource teardown; the `Plugin` state/reason update is queued to the game thread. If quiescence or queuing fails, the host logs the failure and stops the process rather than leaving the provider enabled.

A stack overflow leaves too little stack to walk and is blamed on the owner; faults in `query`, `load`, `unload` and the script runtime keep owner blame. Off Windows (host tests) the default guard doesn't contain faults, and a guard installed with `SetCallGuard` may report the module with an internal hook; without one the owner is blamed.

`DllMain` is not guarded: it runs under the OS loader lock, and unwinding out of it would leave the lock held. Keep `DllMain` empty and do the work in `sco_plugin_load`.

This is partial fault attribution and best-effort damage limitation, **not a process security boundary or recovery from corrupted native state**. A retained table is not safe to call: it may reference state damaged by the fault; cached pointers are not revoked, wrapped or invalidated. Consumers must stop using it and restart the game. Off-thread faults **outside a host guard** are not contained. Stack/heap corruption, `__fastfail` and `/GS` failures may end the process, and a native plugin runs with the game's full rights. If safe host quiescence cannot be established, the host uses a controlled process stop.

### Unloading

`sco::app::Stop` refuses off-game-thread entry and entry during an active callout or reload. It dispatches `game.exit`, then closes runtime admission and cancels queued contexts **before** calling `UnloadAll(list, ops, &runtime)`: new service lookup, posts and command invocations are refused during unloading. Standalone `UnloadAll` is lower-level loader teardown; it does not itself dispatch `game.exit`, close runtime admission or cancel all queued contexts.

`UnloadAll` tears down still-loaded plugins newest first, with [built-ins](#built-in-plugins) after every other plugin. Ordinary native teardown calls guarded `sco_plugin_unload`, then `Release(self)` (including resource hooks), then closes the module. If its service table was handed out, terminal loader teardown still marks it `unloaded` but keeps the DLL mapped, with `[plugin] <id>: kept mapped: its service table was handed out`; the mapping does not preserve released backing state or authorise cached calls. A live-session `UnloadOne`/reload request for such a provider is refused before teardown. A fault in the unload export uses the common fault path: `crashed` cleanup without an exposed table, or restart-required `faulted` retention with one. Faulted instances are not entered by `UnloadAll`; neither their unload export nor resource hooks run. If `Release` fails, the module is also retained and the instance becomes `crashed: release failed: <RESULT>`, since runtime callbacks may remain.

`game.exit` is best effort. The host sends it from the game's own quit path (the game's Quit never reaches the message loop as `WM_QUIT`; see [`sco/app.h`](api.md#scoapph-the-host-kit)), but a crash or a killed process never sends it, and nothing unloads then. A plugin must not rely on `game.exit` or `sco_plugin_unload` for durability: save as it goes.

`LoadNative` passes the plugin path to `LoadLibraryExW` as an absolute path, so the host may discover from a relative root such as `data/plugins`.

### Hot reload

The [service-table lifetime audit](design/service-table-lifetime.md) inventories table owners, query/accessor paths, thread rules and every unload/reload/withdrawal path, including shutdown and fault exceptions.

`sco::app::Reload(id)` (the host command `sco.reload <id>`, [API](api.md#scoapph-the-host-kit), and the product's UI all use this entry point) reloads the target **and every transitively loaded dependent** while the game runs, on the game thread. It uses the `requires` edges resolved at discovery (`Plugin::after`): both `provider` and `provider.svc` identify dependencies, but a resolved host capability/service is not a plugin edge. Unrelated plugins and dependents that are not loaded stay unchanged.

1. **Preflight the whole group:** every member must have reloadable code, idle callouts, loaded requirements and no handed-out service table. A built-in or data dependent refuses the entire cascade. The loader's atomic `BeginUnload` gate then reserves every owner together: either an earlier service query wins and the group is refused, or withdrawal wins and no query can obtain an old table. This happens before copies or teardown.
2. **Prepare native copies:** every native entry DLL is copied to `<folder>/.reload-<n>-<entry>`. Copies sit next to the originals so dependencies still resolve from the plugin folder. (`Platform::loadFromCopies` also makes `Start` map natives from copies; otherwise originals are locked on Windows until the first reload.) If any copy fails, prepared copies are deleted and every reservation is cancelled; all group members keep running unchanged.
3. **Unload in reverse dependency order:** dependents before providers, through `UnloadOne`: guarded `sco_plugin_unload`, then `Release(self)`, which withdraws commands, subscriptions, tasks and services and runs release hooks (including game-pack cleanup and storage closure), then closes the old module. An unload failure stops before tearing down that member's providers and cancels remaining reservations. Already-unloaded members are reloaded where their requirements still permit it.
4. **Load in forward dependency order:** providers before dependents, with stable folder-order ties. Every member gets a new owner handle, with `[settings]` declared again and kept values read, followed by `LoadNative` on its copy or `LoadScript` on the re-read script. `UnmetRequires` runs again before each load: if a provider fails, its dependents are refused without running their code; independent branches can still load. There is no rollback of completed teardown or successful new loads. `plugin.ini` is not re-read, so edits to `requires`, `entry` or settings declarations need a restart.

`Ok` means every group member is loaded again; each successful member logs `reloaded`. `BadArg` refuses a built-in/data target. `Failed` with nothing changed covers a target that is not loaded, a busy member (including a dependent asking to reload its provider from its own command), another reload already running, a non-reloadable dependent, unmet requirements, a copy failure, or a service table in use. The service-lifetime refusal remains `provides a service in use; restart the game to reload it`: it applies to **any** group member, before `sco_plugin_unload` or `Release`. Loaded providers remain intact and their services stay available after refusal. A faulted provider instead stays retained with lookup withdrawn; it cannot be reloaded and the refusal instructs a game restart. Failures after teardown are reported in `why` and `mod.log`; an unload/load fault leaves that member `crashed` or restart-required `faulted`, load refusal leaves it `refused`, and dependent failures name the unavailable provider. Two reloads in one tick run sequentially.

The atomic gate prevents administrative teardown under a handed-out table, including a concurrent first lookup versus unload. Cascading dependents does not revoke a raw table pointer or make a handed-out provider reloadable. Neither refusal nor fault retention proves safe calls across a fault, provider-managed withdrawal/replacement or shutdown. A Faulted provider has no supported in-process recovery. See the lifetime audit for those separate limits. Lua plugins and natives with no handed-out tables can participate in the cascade; Lua itself cannot provide tables.

Limits: handles are never reused and `host::kMaxPlugins` (256) counts every handle for the life of the process, reloads included; at the limit the plugin is left `unloaded`. Command slots are recycled on release (L5), so reloads do not use them up; plugin handles are not recycled, so the 256 are the per-session limit on loads plus reloads (and raw handler records, 1,024 registrations for the process, are not recycled either). Copies are deleted when the next reload (or `Stop`) can; on Windows a copy that is still mapped stays, and `Start` removes every `.reload-*` file of a native plugin folder before it loads, so a run that died leaves nothing behind. The plugin's own state (a file it wrote, its `sco.storage` database) survives; anything it kept in memory does not.

## Lua plugins

`LoadScript(plugin, api, self, runtime)`, on the game thread, with the script runtime the host links in (sc-offline: [sco-lua](../plugins/lua/README.md)):

1. Reads the entry script (1 MiB at most).
2. Calls the runtime's `load` (guarded like a native call). It runs the script once in a sandbox; an error refuses the plugin with the script's message (`main.lua:3: ...`) and releases what it registered.

A script talks to the host only through the `sco_api` table, like a native plugin, and has no file, OS or network access. It runs under a step budget and a 64 MiB memory cap (256 MiB for every script together); past either, or after 3 errors, the runtime disables it (`[<id>] error: script disabled: <why>`). `UnloadAll(list, ops, &runtime)` releases each script, then frees it. What scripts can call: [`sdk/docs/lua.md`](../sdk/docs/lua.md).

## Typed settings (`[settings]`)

A native or Lua plugin can declare settings in a `[settings]` section at the end of its `plugin.ini`. The host keeps their values, keeps them across launches, draws them in the plugin's menu page (the product asks [`sco::settings::Pages()`](ui.md#settings)) and announces each change; the plugin only reads, through the host service `sco.settings` ([API v1](api-v1.md#scosettings-10), Lua `sco.settings.get`). A data pack has no code to read them: `a data pack has no settings`.

```ini
[settings]
god_mode   = bool   default false label "God mode" help "Take no damage"
speed      = int    default 5 min 1 max 10 label "Walk speed"
fov        = float  default 90 min 60 max 120
nickname   = string default "Pilot"
difficulty = enum(easy,normal,hard) default normal
```

One declaration per line: `name = type [default V] [min N] [max N] [label "text"] [help "text"]`. The fields may come in any order, each at most once. A value is a bare word or text in double quotes (no escapes, so no quote inside); `;` or `#` starts a comment outside quotes, at the start of the line or after a space or tab.

| Type | Value | Default when none is given | Notes |
|---|---|---|---|
| `bool` | `true` or `false` | `false` | |
| `int` | An integer in 64 bits, `-12` | `0`, or the nearest bound | `min` / `max` optional |
| `float` | Plain decimal with an optional exponent, `1.5`, `-2e-3`: no `nan`, `inf`, hex, `.5` or `5.` | `0`, or the nearest bound | `min` / `max` optional |
| `string` | At most 255 bytes of printable text (empty is fine) | `""` | |
| `enum(a,b,c)` | One of the choices | The first choice | 1-16 choices of 1-31 of `[a-z0-9_]`, no repeats, spaces after commas allowed |

- `name`: 1-31 of `[a-z0-9_]`, unique in the plugin. At most 32 settings.
- `min` and `max` apply to `int` and `float` only, and `min` may not exceed `max`. The default must lie inside them.
- `label`: what the player sees (1-63 printable characters; the name when there is none). `help`: a hint (1-255).
- **Refused, with the line and the setting:** an unknown field (a typo such as `defualt` is an error, not skipped), a field twice, a field without a value, an unclosed quote, a type that isn't one of the five, a default or bound that isn't a value of its type, a default outside its range, `min` above `max`, `min` / `max` on a type that has no range, a bad or repeated name, more than 32 settings, a repeated `[settings]` section. The reason is the plugin's `refused` line: `plugin.ini: line 9: setting 'speed': default 11 is above max 10`. `sco-plugin-check` reports the same.
- **A host older than this feature** reads `[settings]` as a bad line and refuses the plugin; sco_api.h is unchanged, so it still loads there without the section.

**Values.** `Declare` (called by `sco::app::Start` before the plugin's code runs) reads each kept value from the plugin's own `sco.storage` namespace, key `sco.settings.<name>`, text `<type>:<value>` (`int:7`, `enum:hard`). A kept value that no longer fits the declaration, because the plugin's update changed the type, narrowed the range or dropped a choice, is not used: the default applies, the stale key is deleted, and `mod.log` says `[settings] pilot.speed: kept value dropped (it was saved as int, the setting is string now); the default applies`. Without storage (no data folder) values live in memory and every start begins at the defaults. Keys of settings a plugin no longer declares are left alone. The `sco.settings.` key prefix belongs to the host.

## Data packs

`kind = data` packs carry content and never run code. `ContentIndex::Build(list)` indexes every ready pack and marks it `loaded`:

| Kind | Files |
|---|---|
| `mission` | `missions/*.cwmission` |
| `rules` | `rules/*.rules` |
| `script` | `scripts/**.xml` (any depth) |
| `list` | `lists/*.txt` |
| `datacore` | `datacore/*.toml`: DataCore overrides ([format](datacore.md#pack-format)), applied when the game loads `Game2.dcb` |

Extensions match in any case. Anything else in the folder is ignored, and symlinks are skipped so a pack can't reach outside itself. `scripts/` is read at most 16 folders deep. A pack with more than 4096 matching files is refused (`too many files`), and so is one whose content folders can't be read to the end (`cannot read scripts: ...`), so a pack never loads with only part of its files. `Build` can run again at any time: it re-reads every ready or loaded pack. Features query the index with `Items(kind)`, `Find(kind, "missions/a.cwmission")` (one entry per pack that ships that name, in plugin order) or `FromPlugin(id)`, and read the files themselves.

## Built-in plugins

A feature compiled into the host can be a plugin too: the same three functions a plugin DLL exports, listed in a table instead of loaded from a folder.

```cpp
static const sco::plugins::Builtin kBuiltins[] = {
    { "teleport", TeleportQuery, TeleportLoad, TeleportUnload },   // id, query, load, unload
};
auto p = sco::plugins::FromBuiltin(kBuiltins[0]);                  // kind builtin, state ready
sco::plugins::LoadBuiltin(p, api, sco::host::NewPlugin("teleport"), opts);
```

- No `plugin.ini`: the manifest (name, version, author, api) comes from `sco_plugin_query`. `FromBuiltin` refuses an id that isn't `[a-z0-9_]`, 1-31 characters and unreserved, or a null function.
- `LoadBuiltin` runs `LoadNative`'s checks without a module: `size` covers `author`, the api major matches and the minor isn't newer, `name` equals the id; then `sco_plugin_load(api, self)`. Refusals read `built-in built for api 2.0` and `built-in name 'x' does not match id 'y'`.
- Crash containment is the same: `query`, `load` and `unload` are guarded, and `ContainCallouts` guards every callout it owns, so a built-in that faults is `crashed` (or `faulted` with a handed-out table) and later host callouts are skipped. This is not a native-process recovery guarantee.
- `UnloadAll` unloads built-ins after every other plugin. This is teardown ordering, not permission to call their services during host shutdown: `sco::app::Stop` has already closed runtime admission before unloading, so service lookup and command invocation are refused. Standalone loader-only teardown does not establish that terminal boundary, but neither path authorises cached peer-provider calls from unload callbacks ([plugin rules](../sdk/docs/plugin-rules.md#native-trust-and-supported-usage)).
- A built-in talks to other plugins only through `sco_api`. Unlike an external plugin it may also read sco-core's C++ headers (signature rows, scanners), since it ships and is tested with the core.
- The report lists it as `builtin`: `teleport 1.0.0 builtin loaded`. Hosts normally don't call these directly: `sco::app::Start` ([API](api.md#scoapph-the-host-kit)) loads built-ins before any discovered plugin.

## Status

`Describe(plugin)` gives one line, `hello 1.0.0 native loaded` or `pack 1.0.0 data refused: built for api 2.0`; `LogReport(list, enabled)` writes `[plugin] N found, L loaded (plugins = on)` and one line per plugin.

## Tests

- `tools/test.sh` runs `tests/test_plugins.cpp` on the host under ASan+UBSan: manifest rules, discovery over `tests/fixtures/plugins/tree/`, the content index, and the loader against real shared libraries built from `tests/fixtures/plugins/native/fake_plugin.c`, one per behavior. Off Windows the crash guard is a signal handler the test installs. `TestServices` (provider `svc_provider`, caller `svc_caller`, three more providers) covers service blame and the pin: the provider is Faulted/restart-required and the caller loaded, the command answers `crashed`; release hooks/unload/close do not run for a handed-out provider, further ticks/tasks do not run and new registrations are refused; a stale table into an already faulted provider; an owner fault staying the owner's; the same inside a nested callout; the off-thread verdict; and a counting `ModuleOps.close` for a handed-out and a plain provider. The frame walk itself only runs where the real `__try` guard does: off Windows the test's guard reports the faulting module, so the blame logic is covered there and the walk on Windows (CI's `windows` job; `tools/test-win.sh` under Wine).
- `tools/test.sh` also runs `tests/test_app.cpp`: built-in plugins through the loader (the same checks, crash containment, built-ins unloaded last) and through `sco::app` with the fake plugins, `greeter` and `travel_pack`; the `TestReload` fault case checks lookup withdrawal, stopped ticks, retained module through Stop, faulted reports and restart-required command refusal; then `sco-host-sim` over the SDK examples.
- `tools/test-win.sh` builds the same tests and plugins for Windows with mingw (the guard with clang, since GCC has no `__try`) and runs them under Wine: the real `LoadLibraryExW` and SEH path.
