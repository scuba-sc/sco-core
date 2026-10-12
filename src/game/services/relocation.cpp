// GPL-3.0-only. Admission/transport deliberately fail closed pending grounded G1 support.
#include "relocation.h"
#include "relocation_model.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "spawn.h"
namespace sco::game::services {
namespace {
relocation::Machine g_requests;
bool g_started=false;
sco_result Enter(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self)) return SCO_BAD_ARG;
    return g_started ? SCO_OK : SCO_UNAVAILABLE;
}
sco_result Request(sco_plugin* self,const sc_relocation_request* r,uint64_t* ticket) {
    const auto result=Enter(self);if (result!=SCO_OK) return result;
    if (!r || r->size<sizeof(*r)) return SCO_BAD_ARG;
    // No approved offline/session admission, system classifier or validated transport
    // is supplied by this revision. Do not equate matched read rows with mutation safety.
    const relocation::Evidence evidence{};
    return g_requests.Request(self,*r,evidence,0,ticket);
}
sco_result Cancel(sco_plugin* self,uint64_t ticket) {
    const auto result=Enter(self);return result==SCO_OK ? g_requests.Cancel(self,ticket) : result;
}
sco_result Status(sco_plugin* self,uint64_t ticket,sc_relocation_status* out) {
    const auto result=Enter(self);return result==SCO_OK ? g_requests.Status(self,ticket,out) : result;
}
const sc_relocation_v1 kRelocation{sizeof(sc_relocation_v1),Request,Cancel,Status};
}
Result StartRelocation() {
    if (g_started) return Result::Ok;
    const auto result=host::ProvideGameService(SC_RELOCATION_NAME,SC_RELOCATION_VERSION,&kRelocation);
    if(result==Result::Ok) {
        g_started=true;
        caps::Set(SC_RELOCATION_NAME,false,"offline admission, system classifier and validated relocation transport unavailable");
    }
    return result;
}
void StopRelocation() {
    g_requests.Stop();
    if(g_started) host::WithdrawGameService(SC_RELOCATION_NAME);
    g_started=false;
}
void ReleaseRelocationOwner(const void* owner) { g_requests.Release(owner); }
}
