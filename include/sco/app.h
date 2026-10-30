#pragma once
// The host kit: the startup order every product needs, once, over the game core, caps, the host
// table and the plugin loader. A product (sc-offline, sco-host-sim) supplies a Platform and calls
// Start once on the game thread, Tick from its main-thread hook and Stop when the game closes.
// C++ and internal: plugins only see sco_api.h.
//
//   static const sco::plugins::Builtin kBuiltins[] = { { "teleport", Query, Load, Unload } };
//   static const sco::plugins::ScriptRuntime kLua{ sco_lua_load, sco_lua_unload };
//   sco::SetLogSink(WriteModLog);
//   sco::app::Platform pf;
//   pf.hostVersion = "sc-offline 0.8.0";
//   pf.pluginRoot = "data/plugins";
//   pf.dataRoot = "data";                      // sco.storage: data/storage/<plugin id>.db
//   pf.pluginsEnabled = opts.plugins;
//   pf.builtins = kBuiltins; pf.nBuiltins = std::size(kBuiltins);
//   pf.scripts = &kLua;
//   pf.image = &image;                         // sco::ModuleImage() in game
//   pf.registerSignatures = sco::game::RegisterGameSignatures;   // the game's tables (Star Citizen)
//   pf.setCapabilities = SetFeatureCaps;       // reads the resolved rows
//   sco::app::Start(pf);                       // game thread, after the product's own patches
//   ... sco::app::Tick(nowMs) on every main-thread tick ...
//   sco::app::Stop();                          // game thread, from the game's own quit path
//
// Start, in order:
//   1. SetGameThread (the caller's thread)
//   2. image set: registerSignatures(registry), freeze, resolve and publish a snapshot
//   3. setCapabilities() (after the rows are resolved, so it can use caps::SetFromSignatures)
//   4. host::BuildApi({ hostVersion }); with dataRoot set, storage::Start (sco.storage) and, with
//      dataCore too, datacore::service::Start (sco.datacore); widgets::Start (sco.widgets);
//      ui::Start (sco.ui), surfaces::Start (sco.ui.surfaces) and ui::ReserveChord
//      for each of reservedChords; ipc::Start (sco.ipc); net::Start (sco.net and the capability
//      "sco.net"; no socket until the product calls net::Host or net::Join, sco/net/session.h); with
//      gameServices, game::services::Start (teleport.spatial under the owner "game")
//   5. the list: every built-in (FromBuiltin), then, with pluginsEnabled, Discover(pluginRoot)
//   6. ContainCallouts(list)
//   7. LoadBuiltin for each built-in; then, in list order, LoadNative / LoadScript (a lua plugin
//      is refused "no script runtime" when scripts is null); then the content index
//   8. image set: LogSignatureReport(false); then LogReport
//   9. Dispatch "game.ready"
// Signature registry setup failures refuse startup. Other startup problems are logged and reported: a product with no image, no plugin
// folder or a refused plugin still starts, and every plugin that can load does.
// Tick also writes the owner heartbeat of every sco.ipc channel (ipc::Tick) and delivers sco.net
// messages and events (net::Tick).
// Stop: Dispatch "game.exit", close runtime admissions and cancel all queued work,
// then UnloadAll (newest first, built-ins last), storage::Stop, net::Stop (leaves
// any session), ipc::Stop, widgets::Stop, surfaces::Stop, ui::Stop,
// datacore::service::Stop, game::services::Stop and host::WithdrawGameServices, and
// host::WithdrawHostServices (host and game services outlive every plugin),
// ContainCallouts(nullptr).
#include "sco/plugins.h"
#include "sco/host_menu.h"
#include "sco/diagnostics.h"
#include "sco/diagnostics_policy.h"
#include "sco/runtime.h"
#include "sco/net/control.h"
#include "sco/scan.h"
#include "sco/signature_registry.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sco::app {
// Optional product-owned game-thread service, started before plugins and stopped after
// their unload. A refused start must clean its own partial state; the host continues
// without it. The context outlives Stop, including an unsuccessful stop. Tick and stop
// failures are logged; a product must retain any state that cannot be safely retired.
struct HostService {
    void* context = nullptr;
    Result (*start)(void*) = nullptr;
    Result (*tick)(void*) = nullptr;
    Result (*stop)(void*) = nullptr;
};


struct Platform {
    const char* hostVersion = nullptr;             // "sc-offline 0.8.0"; static string (host_version())
    std::filesystem::path pluginRoot;              // data/plugins
    bool pluginsEnabled = true;                    // configured pluginRoot loads by default; false keeps built-ins only
    const plugins::Builtin* builtins = nullptr;    // the product's own features, loaded first
    size_t nBuiltins = 0;
    const plugins::ScriptRuntime* scripts = nullptr;   // sco-lua, or nullptr: lua plugins refused
    const Image* image = nullptr;                  // resolve signatures against this; nullptr skips
    size_t signatureRegistryBudgetBytes = 0;      // 0 uses allocator limits; see SignatureRegistry budget accounting
    // With image set, Start builds an owned registry, invokes this to register its tables,
    // freezes and resolves it, then installs the complete snapshot. Null registers none.
    // Failure or an existing host registry refuses startup before services or plugins start.
    SigRegistrationResult (*registerSignatures)(SignatureRegistry&) = nullptr;
    void (*setCapabilities)() = nullptr;           // the product sets caps after resolution
    plugins::ModuleOps moduleOps = plugins::PlatformModuleOps();
    // The product's data folder (sc-offline: data/). Set, Start publishes the host service
    // sco.storage (sco/storage.h) with databases in <dataRoot>/storage/; empty, no storage.
    std::filesystem::path dataRoot;
    // Chords the product handles itself ("f6", "ctrl+alt+4"; sco/ui.h ReserveChord): no plugin
    // can bind them to a command. Reserved when sco.ui starts, before any plugin loads; the
    // strings must stay valid during Start.
    const char* const* reservedChords = nullptr;
    size_t nReservedChords = 0;
    // With dataRoot set, Start also publishes the host service sco.datacore (sco_datacore.h,
    // sco/datacore_service.h): plugins queue DataCore overrides, saved in <dataRoot>/datacore/pending/
    // after the load. Off by default: a product turns it on once its CryPak adapter applies the
    // DataCore load (sc-offline: design plan PR 8). Off, query_service answers SCO_NOT_FOUND.
    bool dataCore = false;
    // Start also publishes the Star Citizen game pack's services (sco/game/services.h: today
    // teleport.spatial 1.0) under the owner "game", before any plugin loads, and Stop withdraws
    // them after UnloadAll. They read the game through the teleport.* rows, so the product resolves
    // them first (Platform::registerSignatures, or its own registry resolution). Off by default: a product
    // turns it on once it no longer publishes those names itself. Builds without the game pack's
    // services (SCO_GAME_SC off, or not Windows) log "[app] game services not built" and go on.
    bool gameServices = false;
    // The host must hold a durable barrier before game-service start and before any teardown.
    // True certifies no in-flight/new native readers or target execution throughout that phase,
    // while allowing safe synchronous cleanup reentry. The current quit control frame is excluded.
    // A point-in-time observation is insufficient. Null, false or throw refuses the phase.
    bool (*gameReadersQuiesced)(void*) = nullptr;
    void* gameReadersContext = nullptr;
    // Before any dependent teardown, close product admission and guarantee that product callbacks
    // and cached users remain drained through teardown; false/throw retains all backing state.
    bool (*beforeDependentStop)(void*) = nullptr;
    void* beforeDependentStopContext = nullptr;
    // Start maps each native plugin from a copy of its DLL (<folder>/.reload-<n>-<entry>), as Reload
    // does, so the original can be rebuilt while the game runs. Off: a native is mapped from its own
    // file until its first Reload (on Windows that file is locked until then). A product turns it on
    // for a developer switch. A failed copy logs and falls back to the original.
    bool loadFromCopies = false;
    // Default-off diagnostic sources; no channel/consent inferred from this config.
    diagnostics::SourceOptions diagnosticSources;
    diagnostics::InspectionOptions runtimeInspection; // independently default off
    // G13 is enabled by default but opens no socket without fresh UI consent.
    // Host-only policy: strict loopback unless explicit numeric LAN bind + peers.
    net::ControlPolicyV1 privateSession;
    // Positive EAC/offline/official-network/target evidence; missing or throw refuses.
    // game::host installs its existing admission inspector automatically.
    bool (*privateSessionAdmitted)() = nullptr;
    // Optional product-owned Menu client. Callback context and code outlive Stop.
    HostMenuClient hostMenu;
    HostService hostService;
};

// Starts the host kit (see above). Game thread: it becomes the game thread. False, and nothing
// changes, when already started and not stopped; true otherwise, whatever failed to load.
// `platform` is copied; builtins, scripts and hostVersion must outlive Stop.
bool Start(const Platform& platform);

// GameThreadTick(nowMs): queued tasks, then "tick". No-op unless started.
void Tick(uint32_t nowMs);

// Dispatches "game.exit", then unloads every plugin (UnloadAll: newest first, built-ins last)
// and removes the callout guard. New service lookup/publication, task scheduling,
// events and command/raw invocation are unavailable from the first unload callback until
// the next Start. Queued work is cancelled, not executed; internal owned contexts are dropped.
// Game thread, outside runtime callouts/reload: an unsafe entry is logged and ignored.
// Recursive/sequential Stop is a no-op. The list stays readable
// (final states) until the next Start.
// Call it from the game's own quit path, on the game thread (sc-offline: a hook on the
// system.quit row, CSystem::Quit). Not from a WM_QUIT hook: the game's Quit ends the process
// with "System Fast Shutdown (ExitOnQuit enabled)" and the message loop never sees WM_QUIT.
// Never from DLL_PROCESS_DETACH: that runs under the loader lock, at the wrong time.
// game.exit is best effort: a crash or a killed process never sends it (docs/framework.md,
// Lessons).
// Returns Failed if any host service did not stop; its context and signature registry remain retained.
Result Stop();

// Every plugin of the last Start: built-ins first, then discovered folders. Stable from Start
// to the next Start (never resized in between).
const std::vector<plugins::Plugin>& Plugins();

// Internal host metadata only, never an SDK table or authorization. Any-thread immutable
// completed copy; null during construction and after Stop (invalidated before teardown).
std::shared_ptr<const diagnostics::Snapshot> DiagnosticSnapshot();

// Cascading hot reload on the game thread. The host command "sco.reload <plugin id>" and
// product UI/API callers use this same entry point. Reloads the target and every transitively
// loaded dependent in Discover's resolved `requires` graph; unrelated/non-loaded plugins stay put.
//   1. preflight every member (code kind, idle callouts, loaded requirements, service lifetime),
//      then atomically reserve all owners through the loader's BeginUnload gate. A built-in or
//      data dependent, busy member or handed-out service table refuses the whole group
//      before any copy or teardown. Service queries cannot race this gate to obtain old tables.
//   2. prepare every native DLL copy next to its original. A copy failure deletes prepared copies,
//      cancels every reservation and leaves the whole group running unchanged.
//   3. unload in reverse dependency order (UnloadOne), then load in forward dependency order,
//      checking UnmetRequires again before each load. New handles declare [settings] again and
//      read kept values. Scripts are re-read; plugin.ini is not, so manifest edits need a restart.
// Result (why, when given, says more; each successfully loaded member logs "reloaded"):
//   Ok          every group member is Loaded again with a new self and loadOrder
//   NotFound    no plugin with that folder id
//   BadArg      the target is a built-in or has no reloadable code (including a data pack)
//   Unavailable not started; WrongThread: not the game thread
//   Failed      target not Loaded; busy member/another reload; non-reloadable dependent; native
//               service in use ("provides a service in use; restart the game to reload it"); unmet
//               requirements; failed copy. These refusals leave the group unchanged.
//               Unload failure stops before tearing down its providers and cancels remaining
//               reservations. Already-unloaded members are reloaded where requirements permit.
//               Load refusal/crash/handle exhaustion is not rolled back: dependents with unmet
//               requirements are refused without running code, while independent branches can load.
// Two reloads in one tick run sequentially. Held native tables never change identity mid-session:
// once a table was handed out, its owner cannot join a reload. Command slots are recycled on
// release; plugin handles are not (host::kMaxPlugins bounds every load, including each group member).
Result Reload(std::string_view id, std::string* why = nullptr);

// The data-pack content index of the last Start.
const plugins::ContentIndex& Content();

}  // namespace sco::app
