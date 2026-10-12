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
//   2. image set: registerSignatures() (when set) + ResolveAll(*image)
//   3. setCapabilities() (after the rows are resolved, so it can use caps::SetFromSignatures)
//   4. host::BuildApi({ hostVersion }); with dataRoot set, storage::Start (sco.storage) and, with
//      dataCore too, datacore::service::Start (sco.datacore); ui::Start (sco.ui) and ui::ReserveChord
//      for each of reservedChords; ipc::Start (sco.ipc); net::Start (sco.net and the capability
//      "sco.net"; no socket until the product calls net::Host or net::Join, sco/net/session.h); with
//      gameServices, game::services::Start (teleport.spatial under the owner "game")
//   5. the list: every built-in (FromBuiltin), then, with pluginsEnabled, Discover(pluginRoot)
//   6. ContainCallouts(list)
//   7. LoadBuiltin for each built-in; then, in list order, LoadNative / LoadScript (a lua plugin
//      is refused "no script runtime" when scripts is null); then the content index
//   8. image set: LogSignatureReport(false); then LogReport
//   9. Dispatch "game.ready"
// Startup problems are logged and reported, never fatal: a product with no image, no plugin
// folder or a refused plugin still starts, and every plugin that can load does.
// Tick also writes the owner heartbeat of every sco.ipc channel (ipc::Tick) and delivers sco.net
// messages and events (net::Tick).
// Stop: Dispatch "game.exit", UnloadAll (newest first, built-ins last), storage::Stop, net::Stop (leaves
// any session), ipc::Stop, ui::Stop,
// datacore::service::Stop, game::services::Stop and host::WithdrawGameServices, and
// host::WithdrawHostServices (host and game services outlive every plugin),
// ContainCallouts(nullptr).
#include "sco/plugins.h"
#include "sco/runtime.h"
#include "sco/scan.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sco::app {

struct Platform {
    const char* hostVersion = nullptr;             // "sc-offline 0.8.0"; static string (host_version())
    std::filesystem::path pluginRoot;              // data/plugins
    bool pluginsEnabled = false;                   // plugins = on|off; built-ins load either way
    const plugins::Builtin* builtins = nullptr;    // the product's own features, loaded first
    size_t nBuiltins = 0;
    const plugins::ScriptRuntime* scripts = nullptr;   // sco-lua, or nullptr: lua plugins refused
    const Image* image = nullptr;                  // resolve signatures against this; nullptr skips
    // With image set, Start calls this before ResolveAll: the product registers the signature
    // tables of its game (Star Citizen: sco::game::RegisterGameSignatures). Null registers none, so a
    // host for another game never scans for Star Citizen's rows. False is logged, never fatal.
    bool (*registerSignatures)() = nullptr;
    void (*setCapabilities)() = nullptr;           // the product sets caps after ResolveAll
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
    // them first (Platform::registerSignatures, or its own ResolveAll). Off by default: a product
    // turns it on once it no longer publishes those names itself. Builds without the game pack's
    // services (SCO_GAME_SC off, or not Windows) log "[app] game services not built" and go on.
    bool gameServices = false;
    // Start maps each native plugin from a copy of its DLL (<folder>/.reload-<n>-<entry>), as Reload
    // does, so the original can be rebuilt while the game runs. Off: a native is mapped from its own
    // file until its first Reload (on Windows that file is locked until then). A product turns it on
    // for a developer switch. A failed copy logs and falls back to the original.
    bool loadFromCopies = false;
};

// Starts the host kit (see above). Game thread: it becomes the game thread. False, and nothing
// changes, when already started and not stopped; true otherwise, whatever failed to load.
// `platform` is copied; builtins, scripts and hostVersion must outlive Stop.
bool Start(const Platform& platform);

// GameThreadTick(nowMs): queued tasks, then "tick". No-op unless started.
void Tick(uint32_t nowMs);

// Dispatches "game.exit", then unloads every plugin (UnloadAll: newest first, built-ins last)
// and removes the callout guard. Game thread. No-op unless started. The list stays readable
// (final states) until the next Start.
// Call it from the game's own quit path, on the game thread (sc-offline: a hook on the
// system.quit row, CSystem::Quit). Not from a WM_QUIT hook: the game's Quit ends the process
// with "System Fast Shutdown (ExitOnQuit enabled)" and the message loop never sees WM_QUIT.
// Never from DLL_PROCESS_DETACH: that runs under the loader lock, at the wrong time.
// game.exit is best effort: a crash or a killed process never sends it (docs/framework.md,
// Lessons).
void Stop();

// Every plugin of the last Start: built-ins first, then discovered folders. Stable from Start
// to the next Start (never resized in between).
const std::vector<plugins::Plugin>& Plugins();

// Hot reload of one plugin, on the game thread (the host command "sco.reload <plugin id>", which
// Start registers with no capability, calls this; so can the product's UI).
//   1. natives: the entry DLL is copied to <folder>/.reload-<n>-<entry> (a failed copy changes nothing)
//   2. the plugin unloads as at game exit (UnloadOne: sco_plugin_unload guarded, then Release(self),
//      which withdraws its commands, subscriptions and services, runs the release hooks, so the game
//      pack despawns what it spawned for it; the old module is closed. A native whose service table
//      was handed out never reaches this step: such a request is refused first, before anything of
//      the plugin is torn down — see Failed below)
//   3. a new handle (the one Start makes: [settings] declared again, kept values read) and LoadNative
//      from the copy / LoadScript again (a script is re-read from disk); plugin.ini is not re-read
// Result (why, when given, says more; "reloaded" is logged only when it is Ok):
//   Ok          the plugin is Loaded again (new Plugin::self; loadOrder is the newest)
//   NotFound    no plugin with that folder id
//   BadArg      a built-in (never reloaded) or a data pack
//   Unavailable not started; WrongThread: not the game thread
//   Failed      not Loaded (Refused, Crashed, ...); busy (a callout of the plugin is running, so its code
//               is on the stack: "busy:" in why; nothing changed, retry from outside it; also a reload
//               started from inside another reload); "provides a service in use; restart the game to
//               reload it" (a native whose service table sco::QueryService handed out to another plugin
//               or the host: session-lifetime, refused before unload() or Release, so nothing changed
//               and its services keep resolving; only a restart of the game reloads it); a required
//               plugin is no longer Loaded; the copy failed; unload crashed or could not release
//               (the plugin stays Crashed); no handle left
//               (host::kMaxPlugins counts every load, reloads too: 256 for the life of the process; the
//               plugin stays Unloaded); the new load was refused or crashed (state Refused / Crashed)
// Two reloads in one tick are two full reloads, one after the other. Because a provider whose table
// was ever handed out cannot be reloaded, no consumer can ever see a provider's table change
// identity mid-session; a consumer that re-queries gets a new table only for a provider nobody
// ever asked a service of. Command slots are recycled on release (L5), so reloads do not use them up; plugin handles are not
// recycled: host::kMaxPlugins (256) bounds loads plus reloads for the whole process.
Result Reload(std::string_view id, std::string* why = nullptr);

// The data-pack content index of the last Start.
const plugins::ContentIndex& Content();

}  // namespace sco::app
