// The host kit: Start / Tick / Stop over the game core, caps, the host table and the loader.
// Everything runs on the game thread; the state below is touched by nothing else.
#include "sco/app.h"
#include "owned_start.h"
#include "owned_subsystems.h"
#include "../api/owned_publication.h"
#include "../owned_registry_reservation.h"
#include "../ipc/diagnostics_worker.h"
#include "../api/internal.h"
#include "../plugins/internal.h"
#include "sco/caps.h"
#include "sco/content.h"
#ifndef SCO_KERNEL_ONLY
#include "sco/game/diagnostics.h"
#include "sco/game/private_session.h"
#include "sco/datacore_service.h"   // sco.datacore: the Star Citizen game pack (SCO_GAME_SC)
#endif
#ifdef SCO_GAME_SERVICES
#include "../game/services/mining.h"
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
#include "sco/widgets.h"
#include "sco/surfaces.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace sco::app {

namespace {

std::atomic_flag g_startEntry = ATOMIC_FLAG_INIT;
bool                         g_started = false;
bool                         g_stopping = false;
bool                         g_teardownFailed = false;
bool                         g_gameServicesStartFailed = false;
bool                         g_gameServicesAttempted = false;
bool                         g_hostServiceStarted = false;
std::shared_ptr<SignatureRegistry> g_appSignatureRegistry;
std::shared_ptr<const SignatureRegistry> g_appBorrowedRegistry;
std::shared_ptr<const SignatureRegistry> g_appPinnedRegistry;
bool g_appHostPinHeld = false;
void RevokeCapabilities() noexcept { caps::InvalidateAll("signature host unavailable"); }
Platform                     g_platform;
std::vector<plugins::Plugin> g_list;     // never resized while ContainCallouts points at it
plugins::ContentIndex        g_index;
const sco_api*               g_api = nullptr;   // what Reload loads with
plugins::Options             g_opts;
diagnostics::SnapshotStore   g_diagnostics;
diagnostics::Capture         g_diagnosticCapture;
diagnostics::ipc::Worker     g_diagnosticWorker(g_diagnostics,g_diagnosticCapture);
std::vector<plugins::State>  g_diagnosticStates;
std::optional<uint64_t>       g_lastDiagnosticTick;
bool                         g_gameServicesPublished = false;
bool                         g_reloading = false;
bool                         g_reloadCommand = false;   // sco.reload is registered (once per process)

enum class MenuPhase { Unused, Loading, Active, NormalPending, QuarantinePending, Released, Retained };
struct MenuInstance {
    HostMenuContext identity;
    HostMenuClient client;
    std::atomic<MenuPhase> phase{MenuPhase::Unused};
};
// Instances and their callback identities are never reused, including after failed cleanup.
MenuInstance g_menuInstances[host::kMaxPlugins];
std::atomic<size_t> g_menuCount{0};
MenuInstance* g_menu = nullptr; // game thread
bool g_menuBlocked = false;     // failed cleanup may leave borrowed callbacks reachable

bool MenuGuarded(void (*thunk)(void*), void* context) noexcept {
    try { return plugins::Guarded(thunk, context) == 0; }
    catch (...) { return false; }
}

void MenuFault(MenuInstance& instance, const char* where) {
    instance.phase.store(MenuPhase::Retained, std::memory_order_release);
    g_menuBlocked = true;
    Log("[app] Menu fault in %s; disabling this client", where ? where : "callback");
    Result result = Result::Failed;
    try { result = sco::detail::QuarantineOwner(instance.identity.self); } catch (...) {}
    if (result == Result::Ok)
        instance.phase.store(MenuPhase::QuarantinePending, std::memory_order_release);
    else {
        g_menuBlocked = true;
        Log("[app] Menu fault in %s; quarantine failed, retaining its context", where ? where : "callback");
    }
}

bool MenuCallout(const void* owner, const char* where, TaskFn thunk, void* context) {
    const size_t count = g_menuCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
        MenuInstance& instance = g_menuInstances[i];
        if (instance.identity.self != owner) continue;
        if (!OnGameThread()) return false;
        const MenuPhase phase = instance.phase.load(std::memory_order_acquire);
        if (phase != MenuPhase::Loading && phase != MenuPhase::Active) return false;
        if (!MenuGuarded(thunk, context)) { MenuFault(instance, where); return false; }
        // Nested callbacks can fault even if the outer thunk returned normally.
        if (instance.phase.load(std::memory_order_acquire) != phase) return false;
        if (sco::detail::Released(owner)) {
            instance.phase.store(MenuPhase::Retained, std::memory_order_release);
            g_menuBlocked = true; // an untracked release does not establish hook completion
            return false;
        }
        return true;
    }
    return plugins::detail::ContainedCallout(owner, where, thunk, context);
}

struct MenuCleanup { MenuInstance* instance; Result result = Result::Failed; };
void MenuRelease(void* context) {
    auto& cleanup = *static_cast<MenuCleanup*>(context);
    cleanup.result = sco::Release(cleanup.instance->identity.self);
}
void MenuFaultResources(void* context) {
    auto& cleanup = *static_cast<MenuCleanup*>(context);
    sco::detail::ReleaseFaultResources(cleanup.instance->identity.self);
    cleanup.result = Result::Ok;
}
void DrainMenuCleanup() {
    if (!OnGameThread() || sco::detail::InCallout()) return;
    const size_t count = g_menuCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
        MenuInstance& instance = g_menuInstances[i];
        const MenuPhase phase = instance.phase.load(std::memory_order_acquire);
        if (phase != MenuPhase::NormalPending && phase != MenuPhase::QuarantinePending) continue;
        // Mark before entering hooks so a fault or recursion can never retry them blindly.
        instance.phase.store(MenuPhase::Retained, std::memory_order_release);
        MenuCleanup cleanup{&instance};
        const bool completed = MenuGuarded(phase == MenuPhase::NormalPending ? MenuRelease : MenuFaultResources, &cleanup);
        if (completed && cleanup.result == Result::Ok) {
            instance.phase.store(MenuPhase::Released, std::memory_order_release);
            if (phase == MenuPhase::QuarantinePending) g_menuBlocked = false;
        } else {
            g_menuBlocked = true;
            Log("[app] Menu cleanup incomplete; retaining its context");
        }
        if (g_menu == &instance) g_menu = nullptr;
    }
}

struct MenuLoadCall { MenuInstance* instance; sco_result result = SCO_FAILED; };
void MenuLoadThunk(void* context) {
    auto& call = *static_cast<MenuLoadCall*>(context);
    call.result = call.instance->client.load(call.instance->identity, call.instance->client.context);
}
struct MenuFrameCall { MenuInstance* instance; void* frameContext; };
void MenuFrameThunk(void* context) {
    auto& call = *static_cast<MenuFrameCall*>(context);
    call.instance->client.frame(call.instance->identity, call.instance->client.context, call.frameContext);
}
void MenuStopThunk(void* context) {
    auto& instance = *static_cast<MenuInstance*>(context);
    instance.client.stop(instance.identity, instance.client.context);
}
void StopMenu() {
    MenuInstance* instance = g_menu;
    if (!instance) { DrainMenuCleanup(); return; }
    if (instance->phase.load(std::memory_order_acquire) == MenuPhase::Active ||
        instance->phase.load(std::memory_order_acquire) == MenuPhase::Loading) {
        if (instance->client.stop)
            sco::detail::Callout(instance->identity.self, "Menu stop", MenuStopThunk, instance);
        const MenuPhase phase = instance->phase.load(std::memory_order_acquire);
        if (phase == MenuPhase::Active || phase == MenuPhase::Loading)
            instance->phase.store(MenuPhase::NormalPending, std::memory_order_release);
    }
    DrainMenuCleanup();
}
void StartMenu(const HostMenuClient& client, const sco_api* api) {
    if (!client.load || g_menuBlocked) return;
    const size_t count = g_menuCount.load(std::memory_order_relaxed);
    if (count == host::kMaxPlugins) { Log("[app] Menu instance limit reached"); return; }
    sco_plugin* owner = host::NewMenuOwner();
    if (!owner) { Log("[app] Menu owner unavailable"); return; }
    MenuInstance& instance = g_menuInstances[count];
    instance.identity = {api, owner, static_cast<uint64_t>(count) + 1};
    instance.client = client;
    instance.phase.store(MenuPhase::Loading, std::memory_order_relaxed);
    g_menuCount.store(count + 1, std::memory_order_release);
    g_menu = &instance;
    MenuLoadCall call{&instance};
    const bool completed = sco::detail::Callout(owner, "Menu load", MenuLoadThunk, &call);
    if (completed && call.result == SCO_OK)
        instance.phase.store(MenuPhase::Active, std::memory_order_release);
    else {
        if (completed) Log("[app] Menu load refused: %u", static_cast<unsigned>(call.result));
        StopMenu();
    }
}

uint64_t DiagnosticNow() {
    return diagnostics::ipc::ClockNow();
}
void CopyDiagnostics(uint64_t now) noexcept {
    try {
        diagnostics::HostMetadata host;
        if (g_platform.hostVersion) host.hostVersion = g_platform.hostVersion;
        host.hostBuild = g_platform.runtimeInspection.hostBuild;
        host.sourceSha = g_platform.runtimeInspection.sourceSha;
        host.readiness = "ready";
        diagnostics::LifecycleMetadata game;
        game.lifecycle = "ready"; game.lastTickMs = g_lastDiagnosticTick;
        game.offlineAdmission = diagnostics::Authorized(g_platform.runtimeInspection) ? "authorized" : "unknown";
        game.gameServicesRequested = g_platform.gameServices;
#ifdef SCO_GAME_SERVICES
        game.gameServicesBuilt = true;
#else
        game.gameServicesBuilt = false;
#endif
        game.gameServicesPublished = g_gameServicesPublished;
        diagnostics::OptionalSources sources;
        sources.diagnosticsPlugin = diagnostics::CopyPublisherMetadata(now);
#ifndef SCO_KERNEL_ONLY
        sources.gamePack = game::diagnostics::CopyMinimal(g_platform.diagnosticSources, now);
#endif
        // Copies only: no plugin callbacks, service queries or provider execution.
        g_diagnostics.Begin(g_list, std::move(host), std::move(game), now, std::move(sources));
        g_diagnostics.Advance(now);
        if(diagnostics::Authorized(g_platform.runtimeInspection)){
            if(g_diagnosticStates.size()!=g_list.size())g_diagnosticStates.assign(g_list.size(),plugins::State::Off);
            for(size_t i=0;i<g_list.size();++i){const auto& p=g_list[i];
                const void* key=p.self?static_cast<const void*>(p.self):static_cast<const void*>(&p);
                if(g_diagnosticCapture.AssignKey(key,p.manifest.id)&&g_diagnosticStates[i]!=p.state){
                    diagnostics::HostEvent event=diagnostics::HostEvent::Ready;
                    if(p.state==plugins::State::Loaded)event=diagnostics::HostEvent::Loaded;
                    else if(p.state==plugins::State::Refused)event=diagnostics::HostEvent::Refused;
                    else if(p.state==plugins::State::Faulted)event=diagnostics::HostEvent::Faulted;
                    g_diagnosticCapture.RecordHost(key,event,now);g_diagnosticStates[i]=p.state;
                }
            }
        }else g_diagnosticCapture.Stop();
    } catch (...) {
        g_diagnostics.Reset(); // observation failure must not fail a game/plugin operation
    }
}

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
    const Command c{ "sco.reload", "Reload plugin and dependents", "Reloads a plugin and its loaded requires dependents (native: fresh DLL copies; Lua: scripts run again)",
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

Result ValidateReloadTarget(const std::string& name, plugins::Plugin*& target, std::string& w) {
    target = nullptr;
    for (auto& q : g_list) if (q.folder == name) { target = &q; break; }
    if (!target) { w = "no plugin '" + name + "'"; return Result::NotFound; }
    if (target->manifest.kind == plugins::Kind::Builtin) { w = name + " is built in and cannot be reloaded"; return Result::BadArg; }
    if (!target->manifestOk || target->manifest.kind == plugins::Kind::Data) { w = name + " has no code to reload"; return Result::BadArg; }
    if (target->state == plugins::State::Faulted) { w = name + " is faulted; restart the game (no recovery guaranteed)"; return Result::Failed; }
    if (target->state != plugins::State::Loaded) { w = name + " is not loaded (" + plugins::StateName(target->state) + ")"; return Result::Failed; }
    return Result::Ok;
}

Result BuildReloadGroup(plugins::Plugin* target, std::vector<plugins::Plugin*>& group, std::string& w) {
    // Discover resolved requires to provider ids in `after`; do not reinterpret capabilities
    // or host services as plugin dependencies. Only loaded dependents join the cascade.
    group = { target };
    for (size_t i = 0; i < group.size(); ++i)
        for (auto& q : g_list)
            if (q.state == plugins::State::Loaded &&
                std::find(q.after.begin(), q.after.end(), group[i]->folder) != q.after.end() &&
                std::find(group.begin(), group.end(), &q) == group.end()) group.push_back(&q);

    // Stable topological order over this group, independent of previous reloads' loadOrder.
    std::vector<plugins::Plugin*> ordered;
    ordered.reserve(group.size());
    while (ordered.size() < group.size()) {
        plugins::Plugin* next = nullptr;
        for (auto& q : g_list) {
            if (std::find(group.begin(), group.end(), &q) == group.end() ||
                std::find(ordered.begin(), ordered.end(), &q) != ordered.end()) continue;
            bool ready = true;
            for (const auto* provider : group)
                if (std::find(q.after.begin(), q.after.end(), provider->folder) != q.after.end() &&
                    std::find(ordered.begin(), ordered.end(), provider) == ordered.end()) ready = false;
            if (ready) { next = &q; break; }
        }
        if (!next) { w = "requires cycle in reload group"; return Result::Failed; }
        ordered.push_back(next);
    }
    group = std::move(ordered);
    for (const auto* q : group) {
        if (q->manifest.kind == plugins::Kind::Builtin || q->manifest.kind == plugins::Kind::Data) {
            w = q->folder + " cannot join a reload cascade"; return Result::Failed;
        }
        if (q->running) { w = "busy: a callout of " + q->folder + " is running"; return Result::Failed; }
        if (!plugins::CanUnload(*q, w)) return Result::Failed;
        if (const std::string unmet = plugins::UnmetRequires(*q, g_list); !unmet.empty()) {
            w = unmet; return Result::Failed;
        }
    }
    return Result::Ok;
}

void ReloadPluginInstance(plugins::Plugin& q, fs::path& copy, Result& overallResult, std::string& w, size_t groupSize) {
    if (q.state != plugins::State::Unloaded) return;
    std::error_code ec;
    if (q.module) { q.pinned.push_back(q.module); q.module = nullptr; }
    if (!q.reloadCopy.empty()) fs::remove(q.reloadCopy, ec);
    q.reloadCopy.clear();
    q.exports = {};
    q.state = plugins::State::Ready;
    q.reason.clear();
    std::string error = plugins::UnmetRequires(q, g_list);
    bool ok = false;
    if (!error.empty()) {
        q.state = plugins::State::Refused;
        q.reason = error;
        Log("[plugin] refused %s: %s", q.folder.c_str(), error.c_str());
    } else if (sco_plugin* self = NewSelf(q)) {
        q.reloadCopy = std::move(copy);
        copy.clear();
        ok = q.manifest.kind == plugins::Kind::Native
            ? plugins::LoadNative(q, g_api, self, g_opts, g_platform.moduleOps)
            : g_platform.scripts && plugins::LoadScript(q, g_api, self, *g_platform.scripts);
        if (!ok) {
            error = q.state == plugins::State::Ready ? "no script runtime" : q.reason;
            if (q.state == plugins::State::Ready) {
                q.state = plugins::State::Refused;
                q.reason = error;
            }
        }
    } else {
        q.state = plugins::State::Unloaded;
        error = "no plugin handles left (" + std::to_string(host::kMaxPlugins) + " per process, every load counts)";
        q.reason = error;
    }
    if (ok) Log("[app] reloaded %s", q.folder.c_str());
    else {
        if (overallResult == Result::Ok) w = groupSize == 1 ? error : q.folder + ": " + error;
        overallResult = Result::Failed;
        if (!q.module && !q.reloadCopy.empty()) {
            fs::remove(q.reloadCopy, ec);
            q.reloadCopy.clear();
        }
    }
}

bool InitPlatformAndSignatures(const Platform& pf) {
    if (pf.image) {
        if (!pf.imageOwner || !pf.resolverOwner) return false;
        if (AcquireHostSignatureRegistry()) {
            Log("[app] signature registry already owned");
            return false;
        }
        try {
            auto registry = std::make_shared<SignatureRegistry>(pf.signatureRegistryBudgetBytes);
            if (pf.registerSignatures) {
                const auto result = pf.registerSignatures(*registry);
                if (result.status != SigRegistryStatus::Ok) {
                    Log("[app] signature registration failed: %s (table %zu, row %zu)",
                        SigRegistryStatusName(result.status), result.table, result.row);
                    return false;
                }
            }
            auto status = registry->Freeze();
            if (status == SigRegistryStatus::Ok) status = registry->Resolve(*pf.image, pf.imageOwner, pf.resolverOwner);
            if (status != SigRegistryStatus::Ok) {
                Log("[app] signature resolution failed: %s", SigRegistryStatusName(status));
                return false;
            }
            if (!InstallHostSignatureRegistry(registry, RevokeCapabilities)) {
                Log("[app] signature registry already owned");
                return false;
            }
            g_appSignatureRegistry = std::move(registry);
        } catch (...) {
            Log("[app] signature setup failed");
            return false;
        }
    }
    if (!pf.image) g_appBorrowedRegistry = AcquireHostSignatureRegistry();
    g_appPinnedRegistry = g_appSignatureRegistry ? g_appSignatureRegistry : g_appBorrowedRegistry;
    if (!PinHostSignatureRegistry(g_appPinnedRegistry)) {
        if (g_appSignatureRegistry) {
            if (ClearHostSignatureRegistry(g_appSignatureRegistry, RevokeCapabilities)) g_appSignatureRegistry.reset();
            else g_teardownFailed = true;
        }
        g_appBorrowedRegistry.reset();
        g_appPinnedRegistry.reset();
        Log("[app] signature ownership changed before startup");
        return false;
    }
    g_appHostPinHeld = true;
    try { if (pf.setCapabilities) pf.setCapabilities(); }
    catch (...) {
        bool productReady = true;
        try { if (pf.beforeDependentStop) productReady = pf.beforeDependentStop(pf.beforeDependentStopContext); }
        catch (...) { productReady = false; }
        if (!productReady) {
            if (g_appPinnedRegistry) WithHostSignatureRegistry(g_appPinnedRegistry, RevokeCapabilities);
            else WithNoHostSignatureRegistry(RevokeCapabilities);
            g_teardownFailed = true;
            sco::detail::SetRuntimeStopping(true);
            Log("[app] capability setup failed; dependent state remains retained");
            return false;
        }
        if (g_appSignatureRegistry) {
            if (UnpinAndClearHostSignatureRegistry(g_appSignatureRegistry, RevokeCapabilities)) {
                g_appHostPinHeld = false; g_appPinnedRegistry.reset(); g_appSignatureRegistry.reset();
            } else {
                g_teardownFailed = true;
            }
        } else if (UnpinHostSignatureRegistry(g_appPinnedRegistry)) {
            g_appHostPinHeld = false; g_appPinnedRegistry.reset();
            if (g_appBorrowedRegistry) {
                if (WithHostSignatureRegistry(g_appBorrowedRegistry, RevokeCapabilities)) g_appBorrowedRegistry.reset();
                else g_teardownFailed = true;
            } else if (!WithNoHostSignatureRegistry(RevokeCapabilities)) g_teardownFailed = true;
        } else {
            g_teardownFailed = true;
        }
        Log("[app] capability setup failed");
        return false;
    }
    return true;
}

#ifdef SCO_GAME_SERVICES
bool ConfirmGameReaderBarrier(const Platform& pf) noexcept {
    try { return pf.gameReadersQuiesced && pf.gameReadersQuiesced(pf.gameReadersContext); }
    catch (...) { return false; }
}
bool FinishGameServicesAfterQuiescence() noexcept {
    try {
        if (game::services::Stop() != Result::Ok) return false;
        return game::services::FinalizeStopAfterQuiescence() == Result::Ok;
    } catch (...) { return false; }
}
#endif

Result StartSubsystems(const Platform& pf, const sco::detail::StartupPublish* cap) {
    const auto failed = [cap](Result r) { return cap && (r != Result::Ok || !cap->valid()); };
    if (!pf.dataRoot.empty()) {   // before any plugin loads, built-ins included
        storage::Options so;
        so.dataRoot = pf.dataRoot;
        const Result sr = cap ? storage::owned::Start(so, *cap) : storage::Start(so);
        if (failed(sr)) return sr == Result::Ok ? Result::Unavailable : sr;
        if (sr != Result::Ok) Log("[app] storage not started: %s", ResultName(sr));
        if (pf.dataCore) {
#ifndef SCO_KERNEL_ONLY
            datacore::service::Options dco;
            dco.dataRoot = pf.dataRoot;
            const Result dr = cap ? datacore::service::owned::Start(dco, *cap) : datacore::service::Start(dco);
            if (failed(dr)) return dr == Result::Ok ? Result::Unavailable : dr;
            if (dr != Result::Ok) Log("[app] sco.datacore not started: %s", ResultName(dr));
#else
            Log("[app] sco.datacore not started: this build has no game pack (SCO_GAME_SC=OFF)");
#endif
        }
    }
    // Before consumers load; explicit default-off source config cannot enable IPC.
    if (pf.diagnosticSources.diagnosticsPlugin) {
        const Result dr = diagnostics::StartPublisher(pf.diagnosticSources);
        if (dr != Result::Ok) Log("[app] diagnostics publisher not started: %s", ResultName(dr));
    }
    const Result ur = cap ? ui::owned::Start(*cap) : ui::Start();
    if (failed(ur)) return ur == Result::Ok ? Result::Unavailable : ur;
    if (ur != Result::Ok) Log("[app] ui not started: %s", ResultName(ur));
    const Result sur = cap ? surfaces::owned::Start(*cap) : surfaces::Start();
    if (failed(sur)) return sur == Result::Ok ? Result::Unavailable : sur;
    if (sur != Result::Ok) Log("[app] ui surfaces not started: %s", ResultName(sur));
    const Result wr = cap ? widgets::owned::Start(*cap) : widgets::Start();
    if (failed(wr)) return wr == Result::Ok ? Result::Unavailable : wr;
    if (wr != Result::Ok) Log("[app] widgets not started: %s", ResultName(wr));
    const Result str = cap ? settings::owned::Start(*cap) : settings::Start();
    if (failed(str)) return str == Result::Ok ? Result::Unavailable : str;
    if (str != Result::Ok) Log("[app] settings not started: %s", ResultName(str));
    const Result cr = cap ? content::owned::Start(*cap) : content::Start();
    if (failed(cr)) return cr == Result::Ok ? Result::Unavailable : cr;
    if (cr != Result::Ok) Log("[app] content not started: %s", ResultName(cr));
    const Result ir = cap ? ipc::owned::Start({}, *cap) : ipc::Start();
    if (failed(ir)) return ir == Result::Ok ? Result::Unavailable : ir;
    if (ir != Result::Ok) Log("[app] sco.ipc not started: %s", ResultName(ir));
    net::ServiceOptions networkOptions;
#ifndef SCO_KERNEL_ONLY
    networkOptions.ownerReleased = game::private_session::ReleaseOwner;
#endif
    const Result nr = cap ? net::owned::Start(networkOptions, *cap) : net::Start(networkOptions);
    if (failed(nr)) return nr == Result::Ok ? Result::Unavailable : nr;
#ifndef SCO_KERNEL_ONLY
    const game::private_session::Options sessionOptions{pf.privateSession, pf.privateSessionAdmitted};
    const Result pr = cap ? game::private_session::owned::Start(sessionOptions, *cap) : game::private_session::Start(sessionOptions);
    if (failed(pr)) return pr == Result::Ok ? Result::Unavailable : pr;
    if (pr != Result::Ok) Log("[app] private-session broker not started: %s", ResultName(pr));
#endif
    if (nr != Result::Ok) Log("[app] sco.net not started: %s", ResultName(nr));
    if (pf.gameServices) {
#ifdef SCO_GAME_SERVICES
        if (!ConfirmGameReaderBarrier(pf)) {
            g_gameServicesStartFailed = true;
            Log("[app] game-service startup refused: native reader barrier unavailable");
        } else {
            g_gameServicesAttempted = true;
            Result gr = Result::Failed;
            try { gr = game::services::Start(); } catch (...) {}
            g_gameServicesPublished = gr == Result::Ok;
            if (gr != Result::Ok) {
                g_gameServicesStartFailed = true;
                Log("[app] game services not started: %s", ResultName(gr));
            }
        }
#else
        Log("[app] game services not built (SCO_GAME_SC off, or not Windows)");
#endif
    }
    for (size_t i = 0; pf.reservedChords && i < pf.nReservedChords; ++i) {
        const char* chord = pf.reservedChords[i];
        const Result rr = ui::ReserveChord(chord);
        if (rr != Result::Ok) Log("[app] hotkey '%s' not reserved: %s", chord ? chord : "(null)", ResultName(rr));
    }
    return cr;
}

plugins::Options DiscoverPlugins(const Platform& pf, const sco_api* api, const sco::detail::StartupPublish* cap) {
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
    if (!cap) plugins::ContainCallouts(&g_list);
    return opts;
}

bool PrepareDataPacks(Result cr, const sco::detail::StartupPublish* cap) {
    // Packs are indexed and snapshotted before ANY consumer load callback.
    // A startup pack cannot depend on code that has not run. Refuse that
    // dependency explicitly instead of exposing content from a failed provider.
    for (auto& p : g_list) {
        if (p.state != plugins::State::Ready || p.manifest.kind != plugins::Kind::Data) continue;
        for (const auto& dependency : p.manifest.requires_)
            for (const auto& provider : g_list)
                if (!HasCap(dependency.c_str()) && provider.manifest.kind != plugins::Kind::Data &&
                    (dependency == provider.manifest.id || dependency.starts_with(provider.manifest.id + "."))) {
                    p.state = plugins::State::Refused;
                    p.reason = "startup data pack requires code provider: " + dependency;
                }
    }
    for (const size_t i : plugins::LoadOrder(g_list)) {
        auto& p = g_list[i];
        if (p.state != plugins::State::Ready || p.manifest.kind != plugins::Kind::Data) continue;
        if (const auto why = plugins::UnmetRequires(p, g_list); !why.empty()) {
            p.state = plugins::State::Refused;
            p.reason = why;
        }
    }
    g_index.Build(g_list);
    const Result csr = cr == Result::Ok ? (cap ? content::owned::Snapshot(g_list, g_index, *cap) : content::Snapshot(g_list, g_index)) : cr;
    if (csr != Result::Ok) {
        Log("[app] content snapshot failed: %s; consumers not loaded", ResultName(csr));
        Stop();
        return false;
    }
    return true;
}

void LoadPlugins(const Platform& pf, const sco_api* api, const plugins::Options& opts, const sco::detail::StartupPublish* cap) {
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
    if (!cap) RegisterReloadCommand();
}

void FinalizeStartup(const Platform& pf, const sco::detail::StartupPublish* cap) {
    if (pf.image) { auto snapshot = AcquireHostSignatureSnapshot(); if (snapshot) LogSignatureReport(*snapshot, false); }
    plugins::LogReport(g_list, pf.pluginsEnabled);

    if (cap) return; // no arbitrary callbacks or diagnostic workers in owned startup

    const Result r = Dispatch("game.ready", nullptr);
    if (r != Result::Ok) Log("[app] game.ready: %s", ResultName(r));
    const auto diagnosticNow = DiagnosticNow();
    if (diagnostics::Authorized(pf.runtimeInspection))
        g_diagnosticCapture.Start(diagnosticNow, pf.runtimeInspection.freeFormConsent);
    diagnostics::SetLogCapture(&g_diagnosticCapture);
    g_diagnosticStates.clear();
    CopyDiagnostics(diagnosticNow);
    g_diagnosticWorker.Start(pf.runtimeInspection);
}

}  // namespace

bool HostMenuActive(const HostMenuContext& context) {
    if (!OnGameThread() || !g_started || g_stopping || !g_menu) return false;
    const MenuPhase phase = g_menu->phase.load(std::memory_order_acquire);
    return (phase == MenuPhase::Loading || phase == MenuPhase::Active) &&
           g_menu->identity.api == context.api && g_menu->identity.self == context.self &&
           g_menu->identity.generation == context.generation;
}

bool HostMenuFrame(void* frameContext) {
    if (!OnGameThread() || !g_menu || !HostMenuActive(g_menu->identity) || !g_menu->client.frame)
        return false;
    MenuInstance* instance = g_menu;
    MenuFrameCall call{instance, frameContext};
    const bool completed = sco::detail::Callout(instance->identity.self, "Menu frame", MenuFrameThunk, &call);
    DrainMenuCleanup();
    return completed && HostMenuActive(instance->identity);
}

void HostMenuAbort() {
    if (!OnGameThread() || !g_menu) return;
    const MenuPhase phase = g_menu->phase.load(std::memory_order_acquire);
    if (phase == MenuPhase::Loading || phase == MenuPhase::Active)
        MenuFault(*g_menu, "platform frame");
    DrainMenuCleanup();
}

bool IsHostMenuPanel(const char* id) {
    if (!OnGameThread() || !g_started || !id) return false;
    if (std::strcmp(id, "menu.main") != 0 && std::strcmp(id, "menu.status") != 0) return false;
    const size_t count = g_menuCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i)
        if (widgets::OwnedBy(g_menuInstances[i].identity.self, id)) return true;
    return false;
}

bool HasRetainedGameHooks() noexcept {
#ifdef SCO_GAME_SERVICES
    return game::services::MiningHookRetained();
#else
    return false;
#endif
}

static bool StartImpl(const Platform& platform, const sco::detail::StartupPublish* cap) {
    if (g_startEntry.test_and_set()) return false;
    struct Entry { ~Entry() { g_startEntry.clear(); } } entry;
    if (g_started || g_teardownFailed) {
        try { Log("[app] start refused: already started or teardown incomplete"); } catch (...) {}
        return false;
    }
    try {
    sco::detail::SetGameThreadOwned(cap);
    sco::detail::SetRuntimeStopping(false);
    g_diagnostics.Reset();
    diagnostics::ResetServiceDiagnosticHistory();
    g_lastDiagnosticTick.reset(); g_gameServicesPublished = false;
    g_gameServicesStartFailed = false;
    g_gameServicesAttempted = false;
    g_platform = platform;
    const Platform& pf = g_platform;

    // The strict synthetic host owns its private registry; never borrow or
    // pin an unrelated global snapshot on the owned path.
    const bool signaturesReady = cap ? true : InitPlatformAndSignatures(pf);
    g_platform.imageOwner.reset(); g_platform.resolverOwner.reset();
    if (!signaturesReady) return false;
    const char* version = pf.hostVersion ? pf.hostVersion : "sco-core host";
    if (cap) sco::detail::SetCapabilityCheckOwned(cap, caps::Has);
    const sco_api* api = cap ? host::owned::BuildApi({ version }, *cap) : host::BuildApi({ version });
    if (!api) { if (cap) detail::RetainFailedOwnedStartup(platform.shutdownOwner); return false; }

    const Result cr = StartSubsystems(pf, cap);
    if (cap && (cr != Result::Ok || !cap->valid())) {
        detail::RetainFailedOwnedStartup(platform.shutdownOwner);
        return false;
    }
    if (g_gameServicesStartFailed) {
        g_started = true;
        try { Log("[app] game services left retained state; stopping startup"); } catch (...) {}
        Stop();
        return false;
    }
    const plugins::Options opts = DiscoverPlugins(pf, api, cap);
    g_started = true;

    if (!PrepareDataPacks(cr, cap)) return false;

    // Discovery installs the plugin guard; compose it only after discovery and pack preparation.
    if (!cap) sco::SetCalloutGuard(MenuCallout);
    StartMenu(pf.hostMenu, api);
    g_hostServiceStarted = false;
    if (pf.hostService.start && pf.hostService.tick && pf.hostService.stop) {
        try { g_hostServiceStarted = pf.hostService.start(pf.hostService.context) == Result::Ok; }
        catch (...) { g_hostServiceStarted = false; }
        if (!g_hostServiceStarted) Log("[app] optional host service unavailable");
    }
    LoadPlugins(pf, api, opts, cap);
    FinalizeStartup(pf, cap);
    return true;
    } catch (...) {
        // Startup can have installed consumers even if its aggregate state was
        // never published. Refuse restart before running any fallible report.
        g_teardownFailed = true;
        sco::detail::SetRuntimeStopping(true);
        try {
            if (g_appPinnedRegistry) WithHostSignatureRegistry(g_appPinnedRegistry, RevokeCapabilities);
            else WithNoHostSignatureRegistry(RevokeCapabilities);
        } catch (...) {}
        return false;
    }
}

StartResult StartDetailed(const Platform& platform) {
#ifndef SCO_APP_TEST_SYNTHETIC_GAME_SERVICES
    // An affirmative callback cannot establish durable exclusion of native
    // execution. Refuse before any startup or game-service mutation.
    if (platform.gameServices) return StartResult::RefusedBeforeExposure;
#endif
    // Owned sessions require the host's startup gate; a generic direct caller
    // cannot establish the host phase publication transaction.
    if (platform.shutdownOwner) return StartResult::RefusedBeforeExposure;
    try {
        sco::detail::RuntimeLease admission;
        if (admission.owned() || !sco::detail::RuntimeCanStartLegacy()) return StartResult::RefusedBeforeExposure;
        return StartImpl(platform, nullptr) ? StartResult::Started : StartResult::RetainDependencies;
    } catch (...) {
        // Treat unexpected failure conservatively even when exposure is unclear.
        return StartResult::RetainDependencies;
    }
}
bool Start(const Platform& platform) { return StartDetailed(platform) == StartResult::Started; }

namespace detail {
bool CopyOwnedData(const OwnedShutdown::Transition& transition, const std::shared_ptr<OwnedShutdown>& owner,
                   std::vector<plugins::Plugin>& list, plugins::ContentIndex& index) {
    if (!owner || !transition.valid_for(*owner) || !sco::detail::RuntimeBoundTo(owner)) return false;
    list = g_list;
    index = g_index;
    return transition.valid_for(*owner);
}
void RetainFailedOwnedStartup(const std::shared_ptr<OwnedShutdown>& owner) noexcept {
    if (owner) owner->fault();
    if (g_platform.shutdownOwner != owner) return;
    g_teardownFailed = true;
    sco::detail::SetRuntimeStopping(true);
    try {
        if (owner) caps::owned::InvalidateForOwner(owner, "signature host unavailable");
        else if (g_appPinnedRegistry) WithHostSignatureRegistry(g_appPinnedRegistry, RevokeCapabilities);
        else WithNoHostSignatureRegistry(RevokeCapabilities);
    } catch (...) {}
}
bool StartOwned(const Platform& platform, const OwnedShutdown::Transition& transition) {
    if (!platform.shutdownOwner || !transition.valid_for(*platform.shutdownOwner)) return false;
    const sco::detail::StartupPublish cap(platform.shutdownOwner, transition);
    if (!cap.valid() || !sco::detail::HostRegistryReservedFor(platform.shutdownOwner)) return false;
    if (platform.pluginsEnabled || platform.nBuiltins || platform.scripts || platform.gameServices ||
        platform.hostService.start || platform.hostService.tick || platform.hostService.stop ||
        platform.hostMenu.context || platform.hostMenu.load || platform.hostMenu.frame || platform.hostMenu.stop ||
        platform.setCapabilities || platform.diagnosticSources.diagnosticsPlugin || platform.runtimeInspection.enabled ||
        platform.privateSession.enabled || platform.reservedChords || platform.nReservedChords) {
        platform.shutdownOwner->fault();
        return false;
    }
    const bool started = StartImpl(platform, &cap);
    if (!started || !transition.valid_for(*platform.shutdownOwner)) {
        RetainFailedOwnedStartup(platform.shutdownOwner);
        return false;
    }
    return true;
}
}

void Tick(uint32_t nowMs) {
    if (!g_started || g_stopping || g_teardownFailed) return;
    if (!OnGameThread()) return;
    // A direct app tick has the same lifetime obligation as a host tick.
    auto read = g_platform.shutdownOwner ? g_platform.shutdownOwner->try_read() : OwnedShutdown::Read{};
    if (g_platform.shutdownOwner && !read) {
        detail::RetainFailedOwnedStartup(g_platform.shutdownOwner);
        return;
    }
#ifndef SCO_KERNEL_ONLY
    game::private_session::Tick(); // revoke BEFORE queued consumer work
#endif
    if (g_hostServiceStarted) {
        try { if (g_platform.hostService.tick(g_platform.hostService.context) != Result::Ok) Log("[app] host service tick failed"); }
        catch (...) { Log("[app] host service tick threw"); }
    }
    const Result r = GameThreadTick(nowMs);
    if (g_platform.shutdownOwner && sco::detail::RuntimeStopping()) {
        detail::RetainFailedOwnedStartup(g_platform.shutdownOwner);
        return;
    }
    if (r != Result::Ok) Log("[app] tick: %s", ResultName(r));
    DrainMenuCleanup();
    ipc::Tick();   // the owner heartbeat of every sco.ipc channel
    net::Tick();   // sco.net messages and events received since the last tick
    g_lastDiagnosticTick = DiagnosticNow();
    CopyDiagnostics(*g_lastDiagnosticTick);
}

Result Stop() {
    if (!OnGameThread()) { Log("[app] stop refused: wrong thread"); return Result::WrongThread; }
    if (g_teardownFailed) return Result::Failed;
    if (!g_started) return Result::Ok;
    if (g_stopping) return Result::Unavailable;
    if (sco::detail::InCallout() || g_reloading) { Log("[app] stop refused: active callout or reload"); return Result::Unavailable; }
    g_stopping = true;   // recursive Stop from game.exit or unload is a no-op
    if (g_platform.shutdownOwner) {
        // The existing shutdown sequence invokes arbitrary subscribers, task
        // droppers and unload callbacks and destroys dependent state. It is not
        // a failure-atomic retirement transaction. Close the actual owner and
        // retain the entire app instead of sampling a callback or partially
        // tearing it down. No user cleanup callback is run on this path.
        sco::detail::SetRuntimeStopping(true);
        try {
            if (!g_platform.shutdownOwner->ready())
                (void)g_platform.shutdownOwner->drain_until(
                    std::chrono::steady_clock::now() + std::chrono::seconds(1));
        } catch (...) {}
        g_platform.shutdownOwner->fault();
        g_teardownFailed = true;
        g_stopping = false;
        caps::owned::InvalidateForOwner(g_platform.shutdownOwner, "signature host unavailable");
        try { Log("[app] owned retirement retained: cleanup is not failure-atomic"); } catch (...) {}
        return Result::Failed;
    }
    bool productReady = true;
    try { if (g_platform.beforeDependentStop) productReady = g_platform.beforeDependentStop(g_platform.beforeDependentStopContext); }
    catch (...) { productReady = false; }
#ifdef SCO_GAME_SERVICES
    // A failed startup can have installed providers before the aggregate
    // started flag is set. Their partial cleanup is not proven quiescent.
    if (g_gameServicesAttempted && g_gameServicesStartFailed) productReady = false;
    if (g_gameServicesAttempted && !ConfirmGameReaderBarrier(g_platform)) productReady = false;
#endif
    if (!productReady) {
        if (g_platform.shutdownOwner) g_platform.shutdownOwner->fault();
        g_teardownFailed = true;
        sco::detail::SetRuntimeStopping(true); // close admission without unloading retained providers
        g_stopping = false;
        if (g_appPinnedRegistry) WithHostSignatureRegistry(g_appPinnedRegistry, RevokeCapabilities);
        else WithNoHostSignatureRegistry(RevokeCapabilities);
        try { Log("[app] dependent teardown refused; backing state remains retained"); } catch (...) {}
        return Result::Failed;
    }
    g_diagnosticWorker.Stop(); // revoke and close BEFORE any exit/unload callout
    diagnostics::SetLogCapture(nullptr);
    g_diagnostics.Reset(); // invalidate observations BEFORE game.exit and plugin teardown
    diagnostics::StopPublisher(); // cached publisher calls refuse before any unload export
#ifndef SCO_KERNEL_ONLY
    game::private_session::Stop(); // close sockets/admission before exit/unload callbacks
#endif
    const Result r = Dispatch("game.exit", nullptr);
    if (r != Result::Ok) Log("[app] game.exit: %s", ResultName(r));
    const auto refuseRetirement = [] {
        if (g_platform.shutdownOwner) g_platform.shutdownOwner->fault();
        g_teardownFailed = true;
        sco::detail::SetRuntimeStopping(true);
        g_stopping = false;
        if (g_appPinnedRegistry) WithHostSignatureRegistry(g_appPinnedRegistry, RevokeCapabilities);
        else WithNoHostSignatureRegistry(RevokeCapabilities);
        return Result::Failed;
    };
    // game.exit can fault after the initial proof. Do not unload the first peer
    // or drop any dependent backing on an invalidated owned proof.
    if (g_platform.shutdownOwner && !g_platform.shutdownOwner->ready()) return refuseRetirement();
    StopMenu();  // drain host-owned cleanup even if no subsequent Tick occurs
    sco::detail::StopRuntime();   // no new lookup/work; every queued ctx dropped before unload
    try {
        if (!plugins::TryUnloadAll(g_list, g_platform.moduleOps, g_platform.scripts)) return refuseRetirement();
    } catch (...) { return refuseRetirement(); }
    if (g_platform.shutdownOwner && !g_platform.shutdownOwner->ready()) return refuseRetirement();
    bool registryCleanupSafe = !HasRetainedGameHooks();
    if (g_hostServiceStarted) {
        bool stopped = false;
        try { stopped = g_platform.hostService.stop(g_platform.hostService.context) == Result::Ok; } catch (...) {}
        if (!stopped) {
            Log("[app] host service stop failed; context and dependent backing remain retained");
            return refuseRetirement();
        }
        g_hostServiceStarted = false;
    }
    for (auto& p : g_list)
        if (p.manifest.kind == plugins::Kind::Native) { plugins::RemoveReloadCopies(p); p.reloadCopy.clear(); }
    settings::Stop();   // before storage: it reads and writes through it
    content::Stop();
    storage::Stop();
    net::Stop();   // leaves any session
    ipc::Stop();
    widgets::Stop();
    surfaces::Stop();
    ui::Stop();
#ifndef SCO_KERNEL_ONLY
    datacore::service::Stop();
#endif
#ifdef SCO_GAME_SERVICES
    if (g_gameServicesAttempted) {
        if (!FinishGameServicesAfterQuiescence()) {
            Log("[app] game service cleanup or reader quiescence failed; signature registry remains pinned");
            registryCleanupSafe = false;
        } else g_gameServicesAttempted = false;
    }
#endif
    host::WithdrawGameServices();
    host::WithdrawHostServices();
    plugins::ContainCallouts(nullptr);
    bool retiredOwnRegistry = false;
    if (registryCleanupSafe && g_appHostPinHeld) {
        if (g_appSignatureRegistry) {
            if (UnpinAndClearHostSignatureRegistry(g_appSignatureRegistry, RevokeCapabilities)) {
                g_appHostPinHeld = false; g_appPinnedRegistry.reset();
                g_appSignatureRegistry.reset(); retiredOwnRegistry = true;
            } else registryCleanupSafe = false;
        } else if (UnpinHostSignatureRegistry(g_appPinnedRegistry)) {
            g_appHostPinHeld = false; g_appPinnedRegistry.reset();
        } else registryCleanupSafe = false;
    }
    if (!retiredOwnRegistry) {
        if (g_appSignatureRegistry) {
            if (!WithHostSignatureRegistry(g_appSignatureRegistry, RevokeCapabilities)) registryCleanupSafe = false;
        } else if (g_appBorrowedRegistry) {
            if (!WithHostSignatureRegistry(g_appBorrowedRegistry, RevokeCapabilities)) registryCleanupSafe = false;
            else if (registryCleanupSafe) g_appBorrowedRegistry.reset();
        } else if (!WithNoHostSignatureRegistry(RevokeCapabilities)) registryCleanupSafe = false;
    }
    if (!registryCleanupSafe && g_platform.shutdownOwner) g_platform.shutdownOwner->fault();
    g_teardownFailed = !registryCleanupSafe;
    g_started = false;
    g_stopping = false;
    return registryCleanupSafe ? Result::Ok : Result::Failed;
}

Result Reload(std::string_view id, std::string* why) {
    std::string note;
    std::string& w = why ? *why : note;
    w.clear();
    if (!g_started) { w = "the host kit is not started"; return Result::Unavailable; }
    if (g_stopping) { w = "the host kit is stopping"; return Result::Unavailable; }
    if (!OnGameThread()) { w = "reload runs on the game thread only"; return Result::WrongThread; }
    const std::string name(id);
    if (g_reloading) { w = "busy: another reload is running"; return Result::Failed; }

    plugins::Plugin* target = nullptr;
    const Result targetRes = ValidateReloadTarget(name, target, w);
    if (targetRes != Result::Ok) return targetRes;

    std::vector<plugins::Plugin*> group;
    const Result groupRes = BuildReloadGroup(target, group, w);
    if (groupRes != Result::Ok) return groupRes;

    std::vector<fs::path> copies(group.size());
    // The atomic loader gate reserves every owner before any copy or teardown. Roll back on
    // copy failure, and release surviving reservations before loading fresh owner handles.
    struct ReloadScope {
        const std::vector<plugins::Plugin*>& group;
        std::vector<fs::path>& copies;
        bool reserved = false;
        ~ReloadScope() {
            if (reserved) plugins::CancelUnload(group);
            for (const auto& copy : copies) if (!copy.empty()) {
                std::error_code ec;
                fs::remove(copy, ec);
            }
            g_reloading = false;
        }
    } scope{ group, copies };
    g_reloading = true;
    if (!plugins::BeginUnload(group, w)) return Result::Failed;
    scope.reserved = true;
    for (size_t i = 0; i < group.size(); ++i)
        if (group[i]->manifest.kind == plugins::Kind::Native &&
            !plugins::CopyForReload(*group[i], copies[i], w)) return Result::Failed;

    Result result = Result::Ok;
    for (auto it = group.rbegin(); it != group.rend(); ++it) {
        plugins::UnloadOne(**it, g_platform.moduleOps, g_platform.scripts);
        if ((*it)->state != plugins::State::Unloaded) {
            w = "unload failed: " + (*it)->folder + ": " + (*it)->reason;
            result = Result::Failed;
            break;   // never tear down a provider under a dependent that failed to unload
        }
    }
    plugins::CancelUnload(group);
    scope.reserved = false;
    for (size_t i = 0; i < group.size(); ++i) {
        ReloadPluginInstance(*group[i], copies[i], result, w, group.size());
    }
    if (result != Result::Ok) Log("[app] reload %s failed: %s", name.c_str(), w.c_str());
    return result;
}

const std::vector<plugins::Plugin>& Plugins() {
    sco::detail::RuntimeLease admission;
    static const std::vector<plugins::Plugin> empty;
    return admission && !admission.owned() ? g_list : empty;
}
std::shared_ptr<const diagnostics::Snapshot> DiagnosticSnapshot() {
    sco::detail::RuntimeLease admission;
    if (!admission || admission.startup()) return {};
    return g_diagnostics.Latest();
}

const plugins::ContentIndex& Content() {
    sco::detail::RuntimeLease admission;
    static const plugins::ContentIndex empty;
    return admission && !admission.owned() ? g_index : empty;
}

}  // namespace sco::app
