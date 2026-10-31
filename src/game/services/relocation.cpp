// GPL-3.0-only. game.relocation provider: fail closed unless the host certified offline admission, a
// verified backend supplies a live player, a known star system and a transport, and nothing switched it off.
#include "relocation.h"
#include "relocation_model.h"
#include "sco/game/services.h"
#include "sco/game/host.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
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
RelocationPositionBackend g_position{};
bool g_started=false, g_faulted=false, g_inBackend=false;
std::atomic<bool> g_enabled{true};
uint64_t g_generation=0, g_session=0, g_playerId=0, g_instance=0, g_lifetime=0;
bool g_seen=false, g_capSet=false, g_capReady=false;
const char* g_capReason=nullptr;
bool g_positionCapSet=false,g_positionCapReady=false;
const char* g_positionCapReason=nullptr;
// A release hook can clear a slot during a backend callback. Never resume from its old pointer.
struct Operation {
    const void* owner=nullptr;
    uint64_t ticket=0;
    bool position=false;
    uint64_t lifetime=g_lifetime;
    bool Current() const {
        if(!g_started||lifetime!=g_lifetime||(owner&&detail::Released(owner)))return false;
        return !ticket||(g_requests.PendingOwner()==owner&&g_requests.PendingTicket()==ticket&&
                        (g_requests.PendingPosition()!=nullptr)==position);
    }
};

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
    bool prior;
    BackendScope():prior(g_inBackend) { g_inBackend=true; }
    ~BackendScope() { g_inBackend=prior; }
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
bool PositionInstalled() {
    return g_backend.verified && g_backend.offline && g_backend.player && g_position.verified &&
           g_position.target && g_position.apply && g_position.observe;
}
uint64_t Now(const Operation& operation) {
    if(!operation.Current())return 0;
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
Evidence GatherPlayer(const Operation& operation) {
    Evidence e{};
    e.fault=g_faulted;
    if(!operation.Current())return e;
    if (g_faulted || (!Installed()&&!PositionInstalled()) || !g_enabled.load()) { g_seen=false; return e; }
    const int offline=Guarded([] { return g_backend.offline() ? 1 : 0; });
    if(!operation.Current()){e.fault=g_faulted;return e;}
    if (offline!=1) { e.fault=g_faulted; g_seen=false; return e; }
    e.offline=true;
    RelocationPlayer player{};
    const int found=Guarded([&] { return g_backend.player(&player) ? 1 : 0; });
    if(!operation.Current()){e.fault=g_faulted;return e;}
    if (found<0) { e.fault=true; return e; }
    const bool have=found==1 && player.id && player.instance;
    TrackSession(have,player);
    e.transport=true;
    if (!have) return e;
    e.player=true; e.playerId=player.id; e.session=g_session; e.currentSystem=player.system;
    return e;
}
Evidence Gather(const sc_relocation_request* request,bool wantArrival,const Operation& operation) {
    Evidence e=GatherPlayer(operation);
    e.transport=e.transport&&Installed();
    if (!operation.Current() || !request || !e.player || !e.transport) return e;
    RelocationTarget target{};
    const int seen=Guarded([&] { return g_backend.target(request->zone_id,&target) ? 1 : 0; });
    if(!operation.Current()){e.fault=g_faulted;return e;}
    if (seen<0) { e.fault=true; return e; }
    if (seen==1) { e.streamed=target.streamed; e.targetSystem=target.streamed ? target.system : 0; }
    if (wantArrival && e.streamed) {
        double pos[3]={},rot[4]={};
        const int posed=Guarded([&] { return g_backend.pose(request->zone_id,pos,rot) ? 1 : 0; });
        if(!operation.Current()){e.fault=g_faulted;return e;}
        if (posed<0) { e.fault=true; return e; }
        e.arrived=posed==1 && relocation::Arrived(*request,pos,rot);
    }
    return e;
}
Evidence GatherPosition(const sc_relocation_position_request* request,bool wantArrival,uint64_t instance,const Operation& operation) {
    Evidence e=GatherPlayer(operation);
    e.transport=e.transport&&PositionInstalled();
    if(!operation.Current()||!request||!e.player||!e.transport)return e;
    RelocationPositionTarget target{};
    const int seen=Guarded([&]{return g_position.target(request->zone_id,&target)?1:0;});
    if(!operation.Current()){e.fault=g_faulted;return e;}
    if(seen<0){e.fault=true;return e;}
    if(seen==1){
        e.streamed=target.streamed;
        e.targetSystem=target.streamed?target.system:0;
        e.targetEpoch=target.epoch;e.targetInstance=target.instance;
    }
    if(wantArrival&&e.streamed&&instance&&e.targetInstance==instance) {
        double pos[3]={};
        const int observed=Guarded([&]{return g_position.observe(request->zone_id,instance,pos)?1:0;});
        if(!operation.Current()){e.fault=g_faulted;return e;}
        if(observed<0){e.fault=true;return e;}
        e.arrived=observed==1&&relocation::ArrivedPosition(*request,pos);
    }
    return e;
}
void UpdateOneCapability(const Evidence& e,bool position) {
    const char* reason=nullptr;
    if (!g_started) reason="game.relocation stopped";
    else if (g_faulted) reason="relocation provider fault; process restart required";
    else if (position?!PositionInstalled():!Installed())
        reason=position?"no verified position-only relocation backend installed":"no verified relocation backend installed";
    else if (!g_enabled.load()) reason="relocation switched off by the product or player";
    else if (!e.offline) reason="host offline admission is not certified positive";
    else if (!e.player || !e.session || !e.playerId) reason="no live local player";
    else if (!e.currentSystem) reason="current star system unknown";
    const bool ready=!reason;
    bool& set=position?g_positionCapSet:g_capSet;
    bool& wasReady=position?g_positionCapReady:g_capReady;
    const char*& wasReason=position?g_positionCapReason:g_capReason;
    if(set&&wasReady==ready&&wasReason==reason)return;
    set=true;wasReady=ready;wasReason=reason;
    caps::Set(position?SC_RELOCATION_POSITION_CAPABILITY:SC_RELOCATION_NAME,ready,reason);
}
void UpdateCapability(const Evidence& e) {UpdateOneCapability(e,false);UpdateOneCapability(e,true);}
void Abandon(const Operation& operation,const Evidence& evidence) {
    if(operation.owner&&detail::Released(operation.owner))g_requests.Release(operation.owner);
    if(g_faulted)UpdateCapability(evidence);
}
sco_result Enter(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !sco::host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    if (g_inBackend) return SCO_UNAVAILABLE;
    return g_started ? SCO_OK : SCO_UNAVAILABLE;
}
sco_result Request(sco_plugin* self,const sc_relocation_request* r,uint64_t* ticket) {
    const auto result=Enter(self);if (result!=SCO_OK) return result;
    if (!r || r->size<sizeof(*r)) return SCO_BAD_ARG;
    const sc_relocation_request request=*r;const Operation operation{self};
    Evidence evidence=Gather(&request,false,operation);
    if(!operation.Current()){Abandon(operation,evidence);const auto live=Enter(self);return live==SCO_OK?SCO_UNAVAILABLE:live;}
    const uint64_t now=Now(operation);evidence.fault=evidence.fault||g_faulted;
    if(!operation.Current())Abandon(operation,evidence);
    if(g_faulted)UpdateCapability(evidence);
    const auto live=Enter(self);if(live!=SCO_OK)return live;
    if(!operation.Current())return SCO_UNAVAILABLE;
    UpdateCapability(evidence);
    return g_requests.Request(self,request,evidence,now,ticket);
}
sco_result RequestPosition(sco_plugin* self,const sc_relocation_position_request* r,uint64_t* ticket) {
    const auto result=Enter(self);if(result!=SCO_OK)return result;
    if(!r||r->size<sizeof(*r)||!ticket||!relocation::PositionRequest(*r))return SCO_BAD_ARG;
    const sc_relocation_position_request request=*r;const Operation operation{self};
    Evidence evidence=GatherPosition(&request,false,0,operation);
    if(!operation.Current()){Abandon(operation,evidence);const auto live=Enter(self);return live==SCO_OK?SCO_UNAVAILABLE:live;}
    const uint64_t now=Now(operation);evidence.fault=evidence.fault||g_faulted;
    if(!operation.Current())Abandon(operation,evidence);
    if(g_faulted)UpdateCapability(evidence);
    const auto live=Enter(self);if(live!=SCO_OK)return live;
    if(!operation.Current())return SCO_UNAVAILABLE;
    UpdateCapability(evidence);
    return g_requests.RequestPosition(self,request,evidence,now,ticket);
}
sco_result Cancel(sco_plugin* self,uint64_t ticket) {
    const auto result=Enter(self);return result==SCO_OK ? g_requests.Cancel(self,ticket) : result;
}
sco_result Status(sco_plugin* self,uint64_t ticket,sc_relocation_status* out) {
    const auto result=Enter(self);return result==SCO_OK ? g_requests.Status(self,ticket,out) : result;
}
const sc_relocation_v1_1 kRelocation{sizeof(sc_relocation_v1_1),Request,Cancel,Status,RequestPosition};
}
void SetRelocationEnabled(bool enabled) { sco::detail::LegacyEntry entry; if (entry) g_enabled.store(enabled); }
bool RelocationEnabled() { return g_enabled.load(); }
Result StartRelocation(const RelocationBackend* backend) {
    return StartRelocationV2(backend,nullptr);
}
Result StartRelocationV2(const RelocationBackend* backend,const RelocationPositionBackend* position) {
    if (!OnGameThread()) return Result::WrongThread;
    if (g_started) return Result::Ok;
    if(g_lifetime==UINT64_MAX)return Result::TooMany;
    g_backend=backend ? *backend : RelocationBackend{};
    g_position=position?*position:RelocationPositionBackend{};
    const auto result=sco::host::ProvideGameService(SC_RELOCATION_NAME,SC_RELOCATION_VERSION_1_1,&kRelocation);
    if (result!=Result::Ok) { g_backend={};g_position={};return result; }
    g_started=true;++g_lifetime; g_capSet=false;g_positionCapSet=false;
    UpdateCapability(Gather(nullptr,false,Operation{}));
    return Result::Ok;
}
sco_result SetRelocationBackend(const sco_relocation_backend* backend) {
    if (!backend || backend->size < sizeof(sco_relocation_backend) ||
        !backend->player || !backend->target || !backend->apply || !backend->pose) return SCO_BAD_ARG;
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    // StartRelocation answers Ok for an already started service without installing anything.
    if (g_started) return SCO_UNAVAILABLE;
    RelocationBackend be{};
    be.verified = true;
    be.offline = &host::FreshAdmissionAllowed;
    be.player = backend->player;
    be.target = backend->target;
    be.move = backend->apply;
    be.pose = backend->pose;
    be.now = backend->now;
    return static_cast<sco_result>(StartRelocation(&be));
}
sco_result SetRelocationBackendV2(const sco_relocation_backend_v2* backend) {
    if(!backend||backend->size<sizeof(*backend)||!backend->player||!backend->position_target||
       !backend->apply_position||!backend->observe_position)return SCO_BAD_ARG;
    const bool full=backend->target&&backend->apply&&backend->pose;
    if(!full&&(backend->target||backend->apply||backend->pose))return SCO_BAD_ARG;
    if(!OnGameThread())return SCO_WRONG_THREAD;
    if(g_started)return SCO_UNAVAILABLE;
    RelocationBackend be{};
    be.verified=true;be.offline=&host::FreshAdmissionAllowed;be.player=backend->player;
    be.target=backend->target;be.move=backend->apply;be.pose=backend->pose;be.now=backend->now;
    const RelocationPositionBackend position{true,backend->position_target,backend->apply_position,backend->observe_position};
    return static_cast<sco_result>(StartRelocationV2(&be,&position));
}
void TickRelocation() {
    if (!g_started || g_inBackend) return;
    const void* owner=g_requests.PendingOwner();
    if(owner&&detail::Released(owner))g_requests.Release(owner);
    const sc_relocation_request* pending=g_requests.Pending();
    const sc_relocation_position_request* position=g_requests.PendingPosition();
    const Operation operation{g_requests.PendingOwner(),g_requests.PendingTicket(),position!=nullptr};
    const sc_relocation_request fullCopy=pending?*pending:sc_relocation_request{};
    const sc_relocation_position_request positionCopy=position?*position:sc_relocation_position_request{};
    const uint64_t instance=g_requests.PendingTargetInstance();
    Evidence evidence=position?GatherPosition(&positionCopy,true,instance,operation):Gather(pending?&fullCopy:nullptr,pending!=nullptr,operation);
    if(!operation.Current()){Abandon(operation,evidence);return;}
    const uint64_t now=Now(operation);evidence.fault=evidence.fault||g_faulted;
    if(!operation.Current()){Abandon(operation,evidence);return;}
    UpdateCapability(evidence);
    owner=g_requests.PendingOwner();
    if(owner&&detail::Released(owner)){g_requests.Release(owner);return;}
    if (!g_requests.Tick(now,evidence)) return;
    if(!operation.Current()){Abandon(operation,evidence);return;}
    const int moved=Guarded([&] {
        if(operation.position)return g_position.apply(&positionCopy,instance)?1:0;
        return g_backend.move(&fullCopy)?1:0;
    });
    if(!operation.Current()){Abandon(operation,Evidence{});return;}
    g_requests.Applied(moved==1,moved<0);
    if (moved<0) UpdateCapability(Evidence{});
}
void StopRelocation() {
    g_requests.Stop();
    if (g_started) {
        sco::host::WithdrawGameService(SC_RELOCATION_NAME);
        caps::Set(SC_RELOCATION_NAME,false,"game.relocation stopped");
        caps::Set(SC_RELOCATION_POSITION_CAPABILITY,false,"game.relocation stopped");
    }
    g_started=false; g_seen=false; g_capSet=false; g_backend={};g_position={};g_positionCapSet=false;
}
void ReleaseRelocationOwner(const void* owner) { g_requests.Release(owner); }
}

extern "C" sco_result sco_game_set_relocation_backend(const sco_relocation_backend* backend) {
    return sco::game::services::SetRelocationBackend(backend);
}
extern "C" sco_result sco_game_set_relocation_backend_v2(const sco_relocation_backend_v2* backend) {
    return sco::game::services::SetRelocationBackendV2(backend);
}
