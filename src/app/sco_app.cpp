// The host kit: Start / Tick / Stop over the game core, caps, the host table and the loader.
// Everything runs on the game thread; the state below is touched by nothing else.
#include "sco/app.h"
#include "sco/caps.h"
#ifndef SCO_KERNEL_ONLY
#include "sco/datacore_service.h"   // sco.datacore: the Star Citizen game pack (SCO_GAME_SC)
#endif
#ifdef SCO_GAME_SERVICES
#include "sco/game/services.h"      // the game pack's services (sco_game_services, Windows)
#endif
#include "sco/host.h"
#include "sco/ipc.h"
#include "sco/log.h"
#include "sco/net/session.h"
#include "sco/runtime.h"
#include "sco/settings.h"
#include "sco/signatures.h"
#include "sco/storage.h"
#include "sco/ui.h"
#include <cstdio>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace sco::app {

namespace {

bool                         g_started = false;
Platform                     g_platform;
std::vector<plugins::Plugin> g_list;     // never resized while ContainCallouts points at it
plugins::ContentIndex        g_index;
const sco_api*               g_api = nullptr;   // what Reload loads with
plugins::Options             g_opts;
bool                         g_reloading = false;
bool                         g_reloadCommand = false;   // sco.reload is registered (once per process)

// What a plugin's `requires` may name that is already there at discovery: a capability, or a
// service the host or the game pack published (sco.storage, game.vehicles). Plugin services are
// not: no plugin has loaded yet, so those are ordered instead (plugins::Discover).
int HasCap(const char* name) { return caps::Has(name) || ServiceExists(name) ? 1 : 0; }

// sco.reload <plugin>: the host command over Reload. Owned by the host, no capability, lives for the
// process (the owner is never released); between Stop and the next Start it answers Unavailable.
Result ReloadCommand(const Arg* args, uint32_t, void*, char* reply, uint32_t replySize) {
    std::string why;
    const Result r = Reload(args[0].v.s, &why);
    const std::string text = r == Result::Ok ? std::string("reloaded ") + args[0].v.s : why;
    std::snprintf(reply, replySize, "%s", text.c_str());
    return r;
}

void RegisterReloadCommand() {
    if (g_reloadCommand) return;
    static const ArgDef arg{ "plugin", ArgType::String, "The plugin's id" };
    const Command c{ "sco.reload", "Reload plugin", "Unloads one plugin and loads it again (native: from a fresh copy of its DLL; Lua: the script runs again)",
                     nullptr, &arg, 1, ReloadCommand, nullptr };
    const Result r = RegisterCommand(host::HostOwner(), nullptr, c);
    if (r == Result::Ok) g_reloadCommand = true;
    else Log("[app] sco.reload not registered: %s", ResultName(r));
}

// A plugin's handle, with its [settings] declared (and their kept values read) before its code runs.
sco_plugin* NewSelf(const plugins::Plugin& p) {
    sco_plugin* self = host::NewPlugin(p.manifest.id.c_str());
    if (self && !p.manifest.settings.empty()) {
        const Result r = settings::Declare(self, p.manifest.settings);
        if (r != Result::Ok) Log("[app] %s: settings not declared: %s", p.manifest.id.c_str(), ResultName(r));
    }
    return self;
}

}  // namespace

bool Start(const Platform& platform) {
    if (g_started) {
        Log("[app] start refused: already started");
        return false;
    }
    SetGameThread();
    g_platform = platform;
    const Platform& pf = g_platform;

    if (pf.image) {
        if (pf.registerSignatures && !pf.registerSignatures()) Log("[app] the product's signature tables did not all register");
        ResolveAll(*pf.image);
    }
    if (pf.setCapabilities) pf.setCapabilities();

    const char* version = pf.hostVersion ? pf.hostVersion : "sco-core host";
    const sco_api* api = host::BuildApi({ version });
    if (!pf.dataRoot.empty()) {   // before any plugin loads, built-ins included
        storage::Options so;
        so.dataRoot = pf.dataRoot;
        const Result sr = storage::Start(so);
        if (sr != Result::Ok) Log("[app] storage not started: %s", ResultName(sr));
        if (pf.dataCore) {
#ifndef SCO_KERNEL_ONLY
            datacore::service::Options dco;
            dco.dataRoot = pf.dataRoot;
            const Result dr = datacore::service::Start(dco);
            if (dr != Result::Ok) Log("[app] sco.datacore not started: %s", ResultName(dr));
#else
            Log("[app] sco.datacore not started: this build has no game pack (SCO_GAME_SC=OFF)");
#endif
        }
    }
    const Result ur = ui::Start();
    if (ur != Result::Ok) Log("[app] ui not started: %s", ResultName(ur));
    const Result str = settings::Start();   // after storage: kept values are read from it
    if (str != Result::Ok) Log("[app] settings not started: %s", ResultName(str));
    const Result ir = ipc::Start();
    if (ir != Result::Ok) Log("[app] sco.ipc not started: %s", ResultName(ir));
    const Result nr = net::Start();   // inert until the product calls net::Host or net::Join
    if (nr != Result::Ok) Log("[app] sco.net not started: %s", ResultName(nr));
    if (pf.gameServices) {
#ifdef SCO_GAME_SERVICES
        const Result gr = game::services::Start();
        if (gr != Result::Ok) Log("[app] game services not started: %s", ResultName(gr));
#else
        Log("[app] game services not built (SCO_GAME_SC off, or not Windows)");
#endif
    }
    for (size_t i = 0; pf.reservedChords && i < pf.nReservedChords; ++i) {
        const char* chord = pf.reservedChords[i];
        const Result rr = ui::ReserveChord(chord);
        if (rr != Result::Ok) Log("[app] hotkey '%s' not reserved: %s", chord ? chord : "(null)", ResultName(rr));
    }

    plugins::Options opts;
    opts.enabled = pf.pluginsEnabled;
    opts.has = HasCap;
    opts.builtins = pf.builtins;
    opts.nBuiltins = pf.nBuiltins;

    // The whole list exists before ContainCallouts, so it never moves while the guard holds it.
    std::vector<plugins::Plugin> found;
    if (pf.pluginsEnabled && !pf.pluginRoot.empty()) found = plugins::Discover(pf.pluginRoot, opts);
    std::vector<plugins::Plugin> list;
    list.reserve((pf.builtins ? pf.nBuiltins : 0) + found.size());
    for (size_t i = 0; pf.builtins && i < pf.nBuiltins; ++i) {
        list.push_back(plugins::FromBuiltin(pf.builtins[i]));
        const plugins::Plugin& b = list.back();
        if (b.state == plugins::State::Refused) Log("[plugin] refused %s: %s", b.folder.c_str(), b.reason.c_str());
    }
    for (auto& p : found) {
        if (p.manifest.kind == plugins::Kind::Native) plugins::RemoveReloadCopies(p);   // left by a run that died
        list.push_back(std::move(p));
    }
    g_api = api;
    g_opts = opts;
    g_list = std::move(list);
    g_index.Clear();
    plugins::ContainCallouts(&g_list);
    g_started = true;

    // Built-ins first: they are the product's features, and register before any plugin can.
    for (auto& p : g_list)
        if (p.state == plugins::State::Ready && p.manifest.kind == plugins::Kind::Builtin)
            plugins::LoadBuiltin(p, api, host::NewPlugin(p.manifest.id.c_str()), opts);
    // Then the discovered plugins, after the plugins their `requires` name (ties: folder order).
    for (const size_t i : plugins::LoadOrder(g_list)) {
        plugins::Plugin& p = g_list[i];
        if (p.state != plugins::State::Ready || p.manifest.kind == plugins::Kind::Builtin) continue;
        if (const std::string why = plugins::UnmetRequires(p, g_list); !why.empty()) {
            p.state = plugins::State::Refused;   // a provider that didn't load: refused, never started
            p.reason = why;
            Log("[plugin] refused %s: %s", p.manifest.id.c_str(), p.reason.c_str());
            continue;
        }
        if (p.manifest.kind == plugins::Kind::Native) {
            std::string copyError;
            if (pf.loadFromCopies && !plugins::CopyForReload(p, p.reloadCopy, copyError))
                Log("[plugin] %s: %s; loading the original", p.manifest.id.c_str(), copyError.c_str());
            plugins::LoadNative(p, api, NewSelf(p), opts, pf.moduleOps);
        } else if (p.manifest.kind == plugins::Kind::Lua) {
            if (pf.scripts) {
                plugins::LoadScript(p, api, NewSelf(p), *pf.scripts);
            } else {
                p.state = plugins::State::Refused;
                p.reason = "no script runtime";
                Log("[plugin] refused %s: %s", p.manifest.id.c_str(), p.reason.c_str());
            }
        }
    }
    g_index.Build(g_list);
    RegisterReloadCommand();

    if (pf.image) LogSignatureReport(false);
    plugins::LogReport(g_list, pf.pluginsEnabled);

    const Result r = Dispatch("game.ready", nullptr);
    if (r != Result::Ok) Log("[app] game.ready: %s", ResultName(r));
    return true;
}

void Tick(uint32_t nowMs) {
    if (!g_started) return;
    const Result r = GameThreadTick(nowMs);
    if (r != Result::Ok) Log("[app] tick: %s", ResultName(r));
    ipc::Tick();   // the owner heartbeat of every sco.ipc channel
    net::Tick();   // sco.net messages and events received since the last tick
}

void Stop() {
    if (!g_started) return;
    const Result r = Dispatch("game.exit", nullptr);
    if (r != Result::Ok) Log("[app] game.exit: %s", ResultName(r));
    plugins::UnloadAll(g_list, g_platform.moduleOps, g_platform.scripts);
    for (auto& p : g_list)
        if (p.manifest.kind == plugins::Kind::Native) { plugins::RemoveReloadCopies(p); p.reloadCopy.clear(); }
    settings::Stop();   // before storage: it reads and writes through it
    storage::Stop();
    net::Stop();   // leaves any session
    ipc::Stop();
    ui::Stop();
#ifndef SCO_KERNEL_ONLY
    datacore::service::Stop();
#endif
#ifdef SCO_GAME_SERVICES
    game::services::Stop();
#endif
    host::WithdrawGameServices();
    host::WithdrawHostServices();
    plugins::ContainCallouts(nullptr);
    g_started = false;
}

Result Reload(std::string_view id, std::string* why) {
    std::string note;
    std::string& w = why ? *why : note;
    w.clear();
    if (!g_started) { w = "the host kit is not started"; return Result::Unavailable; }
    if (!OnGameThread()) { w = "reload runs on the game thread only"; return Result::WrongThread; }
    const std::string name(id);
    if (g_reloading) { w = "busy: another reload is running"; return Result::Failed; }
    plugins::Plugin* p = nullptr;
    for (auto& q : g_list) if (q.folder == name) { p = &q; break; }
    if (!p) { w = "no plugin '" + name + "'"; return Result::NotFound; }
    if (p->manifest.kind == plugins::Kind::Builtin) { w = name + " is built in and cannot be reloaded"; return Result::BadArg; }
    if (!p->manifestOk || p->manifest.kind == plugins::Kind::Data) { w = name + " has no code to reload"; return Result::BadArg; }
    if (p->state != plugins::State::Loaded) { w = name + " is not loaded (" + plugins::StateName(p->state) + ")"; return Result::Failed; }
    if (p->running) { w = "busy: a callout of " + name + " is running"; return Result::Failed; }
    if (const std::string unmet = plugins::UnmetRequires(*p, g_list); !unmet.empty()) { w = unmet; return Result::Failed; }

    const bool native = p->manifest.kind == plugins::Kind::Native;
    fs::path copy;   // before anything changes: a failed copy leaves the plugin running
    if (native && !plugins::CopyForReload(*p, copy, w)) return Result::Failed;

    g_reloading = true;
    const fs::path oldCopy = p->reloadCopy;
    plugins::UnloadOne(*p, g_platform.moduleOps, g_platform.scripts);
    Result result = Result::Failed;
    if (p->state != plugins::State::Unloaded) {
        w = "unload failed: " + p->reason;   // Crashed; its module stays mapped
    } else {
        std::error_code ec;
        if (p->module) { p->pinned.push_back(p->module); p->module = nullptr; }   // kept mapped (M1): never reused
        if (!oldCopy.empty()) fs::remove(oldCopy, ec);   // Windows keeps a mapped one; Start removes it later
        p->reloadCopy = copy;
        p->exports = {};
        p->state = plugins::State::Ready;
        p->reason.clear();
        if (sco_plugin* self = NewSelf(*p)) {   // [settings] declared again, kept values read
            const bool ok = native ? plugins::LoadNative(*p, g_api, self, g_opts, g_platform.moduleOps)
                                   : g_platform.scripts && plugins::LoadScript(*p, g_api, self, *g_platform.scripts);
            if (ok) {
                result = Result::Ok;
                Log("[app] reloaded %s", name.c_str());
            } else {
                w = p->state == plugins::State::Ready ? "no script runtime" : p->reason;
                p->state = p->state == plugins::State::Ready ? plugins::State::Refused : p->state;
            }
        } else {
            p->state = plugins::State::Unloaded;
            w = "no plugin handles left (" + std::to_string(host::kMaxPlugins) + " per process, every load counts)";
        }
    }
    if (result != Result::Ok && !p->module && !copy.empty()) {   // the copy was never mapped, or is closed
        std::error_code ec;
        fs::remove(copy, ec);
        p->reloadCopy.clear();
    }
    g_reloading = false;
    if (result != Result::Ok) Log("[app] reload %s failed: %s", name.c_str(), w.c_str());
    return result;
}

const std::vector<plugins::Plugin>& Plugins() { return g_list; }

const plugins::ContentIndex& Content() { return g_index; }

}  // namespace sco::app
