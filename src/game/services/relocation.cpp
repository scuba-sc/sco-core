// GPL-3.0-only. game.relocation provider: fail closed unless the host certified offline admission, a
// verified backend supplies a live player, a known star system and a transport, and nothing switched it off.
#include "relocation.h"
#include "relocation_model.h"
#include "sco/game/services.h"
#include "sco/host.h"
#include "sco/caps.h"
#include <atomic>
#include <chrono>
#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>
#endif
namespace sco::game::services {
namespace {
using relocation::Evidence;
relocation::Machine g_requests;
RelocationBackend g_backend{};
bool g_started=false, g_faulted=false, g_inBackend=false;
std::atomic<bool> g_enabled{true};
uint64_t g_generation=0, g_session=0, g_playerId=0, g_instance=0;
bool g_seen=false, g_capSet=false, g_capReady=false;
const char* g_capReason=nullptr;

using Thunk=int(*)(void*);
int RunCpp(Thunk thunk,void* context) {
    try { return thunk(context); } catch (...) { return -1; }
}
int RunGuarded(Thunk thunk,void* context) {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return RunCpp(thunk,context); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
#else
    return RunCpp(thunk,context);
#endif
}
struct BackendScope {
    BackendScope() { g_inBackend=true; }
    ~BackendScope() { g_inBackend=false; }
};
// 1 true, 0 false, -1 the callback threw or faulted (the provider then latches off).
template <class F> int Guarded(F call) {
    BackendScope scope;
    const int result=RunGuarded([](void* context) -> int { return (*static_cast<F*>(context))(); },&call);
    if (result<0) g_faulted=true;
    return result;
}
bool Installed() {
    return g_backend.verified && g_backend.offline && g_backend.player && g_backend.target &&
           g_backend.move && g_backend.pose;
}
uint64_t Now() {
    if (g_backend.now) {
        uint64_t value=0;
        if (Guarded([&] { value=g_backend.now(); return 1; })==1) return value;
    }
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
// A session is one uninterrupted observation of one live player entity: losing the player, offline
// admission or the relocation switch ends it, and so does a different player id or entity token.
void TrackSession(bool have,const RelocationPlayer& player) {
    if (!have) { g_seen=false; return; }
    if (!g_seen || player.id!=g_playerId || player.instance!=g_instance) {
        ++g_generation; g_playerId=player.id; g_instance=player.instance; g_seen=true;
    }
    g_session=g_generation;
}
// Engine reads happen only after a fresh positive offline answer, and never while switched off.
Evidence Gather(const sc_relocation_request* request,bool wantArrival) {
    Evidence e{};
    e.fault=g_faulted;
    if (!g_started || g_faulted || !Installed() || !g_enabled.load()) { g_seen=false; return e; }
    const int offline=Guarded([] { return g_backend.offline() ? 1 : 0; });
    if (offline!=1) { e.fault=g_faulted; g_seen=false; return e; }
    e.offline=true;
    RelocationPlayer player{};
    const int found=Guarded([&] { return g_backend.player(&player) ? 1 : 0; });
    if (found<0) { e.fault=true; return e; }
    const bool have=found==1 && player.id && player.instance;
    TrackSession(have,player);
    e.transport=true;
    if (!have) return e;
    e.player=true; e.playerId=player.id; e.session=g_session; e.currentSystem=player.system;
    if (!request) return e;
    RelocationTarget target{};
    const int seen=Guarded([&] { return g_backend.target(request->zone_id,&target) ? 1 : 0; });
    if (seen<0) { e.fault=true; return e; }
    if (seen==1) { e.streamed=target.streamed; e.targetSystem=target.streamed ? target.system : 0; }
    if (wantArrival && e.streamed) {
        double pos[3]={},rot[4]={};
        const int posed=Guarded([&] { return g_backend.pose(request->zone_id,pos,rot) ? 1 : 0; });
        if (posed<0) { e.fault=true; return e; }
        e.arrived=posed==1 && relocation::Arrived(*request,pos,rot);
    }
    return e;
}
void UpdateCapability(const Evidence& e) {
    const char* reason=nullptr;
    if (!g_started) reason="game.relocation stopped";
    else if (g_faulted) reason="relocation provider fault; process restart required";
    else if (!Installed()) reason="no verified relocation backend installed";
    else if (!g_enabled.load()) reason="relocation switched off by the product or player";
    else if (!e.offline) reason="host offline admission is not certified positive";
    else if (!e.player || !e.session || !e.playerId) reason="no live local player";
    else if (!e.currentSystem) reason="current star system unknown";
    const bool ready=!reason;
    if (g_capSet && g_capReady==ready && g_capReason==reason) return;
    g_capSet=true; g_capReady=ready; g_capReason=reason;
    caps::Set(SC_RELOCATION_NAME,ready,reason);
}
sco_result Enter(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self)) return SCO_BAD_ARG;
    if (g_inBackend) return SCO_UNAVAILABLE;
    return g_started ? SCO_OK : SCO_UNAVAILABLE;
}
sco_result Request(sco_plugin* self,const sc_relocation_request* r,uint64_t* ticket) {
    const auto result=Enter(self);if (result!=SCO_OK) return result;
    if (!r || r->size<sizeof(*r)) return SCO_BAD_ARG;
    const Evidence evidence=Gather(r,false);
    UpdateCapability(evidence);
    return g_requests.Request(self,*r,evidence,Now(),ticket);
}
sco_result Cancel(sco_plugin* self,uint64_t ticket) {
    const auto result=Enter(self);return result==SCO_OK ? g_requests.Cancel(self,ticket) : result;
}
sco_result Status(sco_plugin* self,uint64_t ticket,sc_relocation_status* out) {
    const auto result=Enter(self);return result==SCO_OK ? g_requests.Status(self,ticket,out) : result;
}
const sc_relocation_v1 kRelocation{sizeof(sc_relocation_v1),Request,Cancel,Status};
}
void SetRelocationEnabled(bool enabled) { g_enabled.store(enabled); }
bool RelocationEnabled() { return g_enabled.load(); }
Result StartRelocation(const RelocationBackend* backend) {
    if (!OnGameThread()) return Result::WrongThread;
    if (g_started) return Result::Ok;
    g_backend=backend ? *backend : RelocationBackend{};
    const auto result=host::ProvideGameService(SC_RELOCATION_NAME,SC_RELOCATION_VERSION,&kRelocation);
    if (result!=Result::Ok) { g_backend={}; return result; }
    g_started=true; g_capSet=false;
    UpdateCapability(Gather(nullptr,false));
    return Result::Ok;
}
void TickRelocation() {
    if (!g_started || g_inBackend) return;
    const sc_relocation_request* pending=g_requests.Pending();
    const Evidence evidence=Gather(pending,pending!=nullptr);
    UpdateCapability(evidence);
    if (!g_requests.Tick(Now(),evidence)) return;
    const sc_relocation_request* request=g_requests.Pending();
    if (!request) return;
    const int moved=Guarded([&] { return g_backend.move(request) ? 1 : 0; });
    g_requests.Applied(moved==1,moved<0);
    if (moved<0) UpdateCapability(Evidence{});
}
void StopRelocation() {
    g_requests.Stop();
    if (g_started) {
        host::WithdrawGameService(SC_RELOCATION_NAME);
        caps::Set(SC_RELOCATION_NAME,false,"game.relocation stopped");
    }
    g_started=false; g_seen=false; g_capSet=false; g_backend={};
}
void ReleaseRelocationOwner(const void* owner) { g_requests.Release(owner); }
}

#if defined(_MSC_VER)
__declspec(selectany) bool (*g_sco_relocation_admission_hook)(void) = nullptr;
#else
__attribute__((weak)) bool (*g_sco_relocation_admission_hook)(void) = nullptr;
#endif

static bool FreshHostAdmission() {
    if (g_sco_relocation_admission_hook) return g_sco_relocation_admission_hook();
    return true;
}

static int (*g_cApply)(const sc_relocation_request*) = nullptr;

static bool AdaptRelocationApply(const sc_relocation_request* req) {
    return g_cApply ? (g_cApply(req) != 0) : false;
}

extern "C" sco_result sco_game_set_relocation_backend(const sco_relocation_backend* backend) {
    if (!backend || backend->size < sizeof(sco_relocation_backend)) return SCO_BAD_ARG;
    if (!backend->apply) return SCO_BAD_ARG;
    g_cApply = backend->apply;
    sco::game::services::RelocationBackend be{};
    be.verified = true;
    be.offline = FreshHostAdmission;
    be.move = AdaptRelocationApply;
    return sco::game::services::StartRelocation(&be) == sco::Result::Ok ? SCO_OK : SCO_FAILED;
}
