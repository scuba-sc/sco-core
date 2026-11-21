# Framework: how sco-core, plugins and products fit

Background on how sco-core and sc-offline were split. For what is built now, read [architecture](architecture.md), [game services](game-services.md) and [game capability status](game/status.md).

## Goal

sco-core is the runtime, the plugin system, the services and the SDK that every mod is built from. A product such as sc-offline is a thin bootstrap (inject, patch the game offline, find the main thread, draw a menu) plus a set of plugins. sc-offline's own features are plugins built into its DLL. They use the same plain-C API ([`sco_api.h`](api-v1.md)) as third-party plugins, so the shipped code exercises the API. Another mod suite is another set of plugins on the same core, with sc-offline's bootstrap or its own.

Scope: offline play is the default and the game never connects to Cloud Imperium Games' servers. Local IPC with other programs on the same PC, and private LAN or VPN co-presence between sc-offline players through `sco.net`, are allowed. These are banned for good: online play, getting around anti-cheat, signature-check bypass, forcing the game's host type, unauthenticated remote commands, telemetry, and anything that helps cheating. See the [plugin rules](../sdk/docs/plugin-rules.md) and the [multiplayer design](design/multiplayer.md).

## Architecture

```text
 product      sc-offline bootstrap: dinput8 proxy, offline patches, anti-cheat check,
              main-thread hook, ImGui frontend              (another product: its own bootstrap)
                   |  sco::app::Start / Tick / Stop
 sco-core   +------+----------------------------------------------------------------+
 (the heart)| host kit     startup order, built-in plugins, plugin manager, frontend model |
            | services     store (kv + sql), settings, game services, plugin-provided ones |
            | sco_api.h    the versioned plain-C ABI every plugin uses                     |
            | runtime      tasks, events, commands, owners, crash containment              |
            | game core    signatures, scanners, report                                    |
            +------+----------------------------------------------------------------+
                   |
 plugins      built-in (compiled into the product)   native DLL   Lua script   data pack
```

- **Host kit.** The startup sequence every product needs lives in sco-core once, not in each product's `dllmain.cpp`: log sink, signatures, capabilities, the `sco_api` table, built-in plugins, discovered plugins, the content index, the report, `game.ready`; then the tick; then `game.exit` and unload. The product supplies only what is specific to it, through `sco::app::Platform` ([API: `sco/app.h`](api.md#scoapph-the-host-kit)).
- **Built-in plugins.** A feature compiled into the product exports the same three functions as a plugin DLL (`sco_plugin_query`, `sco_plugin_load`, `sco_plugin_unload`) and is listed in a table instead of loaded from a folder. The loader runs the same checks and the same crash guard, and the report lists it as `builtin`. A built-in plugin talks to other plugins only through `sco_api`. Unlike an external one, it may also read sco-core's internal C++ headers (signature rows, scanners), because it ships and is tested with the core. See [Plugins: built-in plugins](plugins.md#built-in-plugins).
- **Services.** Functionality one part provides to others behind a versioned table. The host provides storage, settings, UI, widgets, DataCore, IPC, net and the game services; plugins can provide their own ([API v1 § Services](api-v1.md#services-11)).
- **Headless host.** `sco-host-sim` runs the real host kit, runtime and loader outside the game. Signatures resolve against a `StarCitizen.exe` on disk (as `sco-sigcheck` does) or a synthetic image; capabilities come from flags; ticks and events come from a script; commands are invoked from the command line. CI uses it to load built-in and external plugins on Linux and Windows.
- **Frontend model.** Plugins publish renderer-neutral panels through `sco.widgets` ([UI](ui.md)); the product renders them. sc-offline renders with ImGui. ImGui never crosses the ABI.

## Decisions

| Decision | Choice |
|---|---|
| Host features' command prefixes | Reserved by built-in plugins. Built-ins load before any discovered plugin and register their commands first, so each owns its id as a command prefix: a plugin folder with the same id gets `SCO_BAD_ARG` from `register_command` for any command under that prefix, and is refused if its load fails on that. A disabled or refused built-in leaves its prefix free |
| Signal for `game.exit` | The game's own quit path, on the game thread. Not `WM_QUIT`, and never `DLL_PROCESS_DETACH` ([lesson 1](#1-the-game-quits-without-wm_quit)) |
| Storage engines | Local and embedded only. Network databases (Postgres, MySQL, Redis, ...) are out of scope: storage never leaves the PC, and `sco.net` co-presence is the only network path ([plugin rules](../sdk/docs/plugin-rules.md)) |

## Lessons

Each lesson is a rule that came out of building the first product on the core. **Lessons flow back to sco-core:** a product-side workaround that reflects a sco-core gap becomes a sco-core issue or pull request (docs, API, a test or a signature row) in the same cycle, and gets an entry here. How: [CONTRIBUTING § Lessons flow back](../CONTRIBUTING.md#lessons-flow-back).

### 1. The game quits without WM_QUIT

The game's menu Quit calls `CSystem::Quit`, then the process ends through the fast-shutdown path without the message loop getting `WM_QUIT`. A `WM_QUIT` hook never runs `sco::app::Stop`, and plugins never see `game.exit`.

**Rule for hosts:** call `sco::app::Stop` from the game's own quit path, on the game thread. The `system.quit` signature row finds `CSystem::Quit` (`sco/game/system.h`, `QuitFunction`), and sc-offline hooks it. Never call `Stop` from `DLL_PROCESS_DETACH`: it runs under the loader lock. A direct detour on the quit function that only requests non-draining closure does not run `Stop`'s sequence or dispatch `game.exit` ([game host](game/host.md#direct-engine-quit-and-dependent-providers)).

**Rule for plugins:** `game.exit` is best effort. A crash or a killed process never sends it, so do not rely on it for durability. See [`sco/app.h`](api.md#scoapph-the-host-kit) and [Plugins § Unloading](plugins.md#unloading).

### 2. The host kit starts whatever any feature does

In sc-offline the main-thread hook, and so the host kit and every plugin, once started only when teleport resolved its addresses. A game build that broke teleport also switched off every plugin.

**Rule for hosts:** start the host kit regardless of any one feature. A feature that fails reports itself unready through a capability. The plugins that need it are refused or grey out, and the others keep running.

### 3. Adopting sco-core's types: aliases don't share names

When a product swaps its own types for sco-core's, an alias is the smallest change (`using Section = sco::Section`). An alias cannot share its name with a function the way a struct could. sc-offline's `menu.cpp` had a `Section()` helper beside `struct Section`, and with the alias it no longer compiled.

**Rule for adoption:** before aliasing, look for functions or variables with the type's name and rename them in the same change. This is why sco-core keeps its names in `namespace sco`: a product chooses which names to pull in.

### 4. Building sco-core inside a product

sc-offline adds sco-core with `add_subdirectory(external/sco-core EXCLUDE_FROM_ALL)` after setting `CMAKE_MSVC_RUNTIME_LIBRARY`, so the static CRT reaches sco-core's libraries too. It sets `SCO_BUILD_TESTS OFF` (sco-core's own CI runs them) and `SCO_WERROR OFF` (a warning only a newer toolset emits must not break the product build).

**Rule:** the CMake settings a product must make are in [Building § Using sco-core from another CMake project](building.md#using-sco-core-from-another-cmake-project).

### 5. Windows shows a dialog for a bad DLL

`LoadLibraryExW` on a file that is not a valid PE (a broken or wrong-architecture plugin) raised a modal "Bad Image" box over the game. The loader now calls it under `SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)`, so the plugin is refused with a reason instead.

**Rule:** plugin loading never shows UI. Every failure is a reason in `mod.log` and `status`. See [Plugins § Native plugins](plugins.md#native-plugins).

### 6. Services hand out ids, never pointers

Star Citizen streams objects in and out, so a pointer a plugin keeps across ticks dangles once its object streams out, and the next call through it faults in the caller. The ids are not small indexes either: in the sc-offline spawn test the player's entity id is `0xCAE11A7400000000`, a tagged value.

**Rule for services:** take and return entity and zone ids, opaque `uint64_t` values as the game's are. Pass them back and never decode them. Resolve the id on every call, on the game thread, and answer `SCO_NOT_FOUND` or `SCO_UNAVAILABLE` when the entity has streamed out. Never hand out a game pointer. See [API § Services](api-v1.md#services-11) and [C++ SDK § Services](sdk-cpp.md#services).

### 7. Signature registry: whole batches, owned snapshots

A fixed process-wide signature array made the game pack's rows and the product's rows compete for 512 slots, and a failure could commit a prefix of a table. The owned registry registers complete batches, freezes its definitions and publishes an immutable per-image snapshot. The host coordinates address lifetime and capability invalidation separately; a snapshot does not lease game memory.

**Rule:** register a table as one batch or not at all, and keep the image and provider code valid until dependent services have stopped. See the [registry contract](api.md#scosignaturesh-and-scosignature_registryh-the-registry).

## Where to read next

- [Architecture](architecture.md): resolution, startup, the runtime and the layers.
- [Plugins](plugins.md): discovery, load order, crash containment, hot reload.
- [Game services](game-services.md): the game pack's service tables.
- [Game capability status](game/status.md): what is on by default and how far each capability has been checked.
- [Launcher contract](launcher-contract.md): what a launcher built on the SDK does.
