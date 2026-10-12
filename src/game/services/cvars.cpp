// SPDX-License-Identifier: GPL-3.0-only
#include "cvars.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#include <cstring>
namespace sco::game::services {
namespace {
CvarsPort g_port;
bool g_started = false;
bool g_calling = false;
bool g_faulted = false; // process-lifetime: never reset an untrusted adapter
struct CallScope { CallScope() { g_calling = true; } ~CallScope() { g_calling = false; } };
constexpr const char* kNames[] = {
    "i_target_selector.targeting2_enabled", "pl_lensdisplay.minimap_enabled",
    "pl_lensdisplay.greebles_enabled", "g_squadron_frontend"
};
int Index(const char* name) {
    if (!name) return -1;
    for (int i = 0; i != 4; ++i) if (std::strcmp(name, kNames[i]) == 0) return i;
    return -1;
}
sco_result Get(sco_plugin* self, const char* name, uint32_t type, uint32_t* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    const int index = Index(name);
    if (index < 0 || type != SC_CVAR_BOOL || !out) return SCO_BAD_ARG;
    if (!g_started || g_faulted || detail::RuntimeStopping() || !g_port.verified || !g_port.offline_admitted ||
        !g_port.read_bool || !caps::Has("game.cvars.read")) return SCO_UNAVAILABLE;
    if (g_calling) return SCO_FAILED;
    CallScope scope;
    uint32_t value = 0;
    const sco_result r = g_port.read_bool(g_port.ctx, static_cast<uint32_t>(index), &value);
    if (r == SCO_CRASHED) { g_faulted = true; caps::Set("game.cvars.read", false, "restart required"); }
    if (r != SCO_OK) return r;
    if (value > 1) return SCO_FAILED;
    *out = value;
    return SCO_OK;
}
const sc_cvars_v1 kTable{sizeof(sc_cvars_v1), 0, Get};
}
Result SetCvarsPort(const CvarsPort& port) {
    if (!OnGameThread()) return Result::WrongThread;
    if (g_faulted) return Result::Failed;
    if (g_started) return Result::BadArg;
    g_port = port;
    return Result::Ok;
}
Result StartCvars() {
    if (!OnGameThread()) return Result::WrongThread;
    if (g_faulted) return Result::Failed;
    if (g_started) return Result::BadArg;
    caps::Set("game.cvars.read", false, "adapter unverified or service not published");
    const Result r = host::ProvideGameService(SC_CVARS_NAME, SC_CVARS_VERSION, &kTable);
    if (r != Result::Ok) return r;
    g_started = true;
    caps::Set("game.cvars.read", g_port.verified && g_port.offline_admitted && g_port.read_bool,
              "verified offline adapter required");
    return Result::Ok;
}
void StopCvars() {
    if (!OnGameThread() || !g_started) return;
    caps::Set("game.cvars.read", false, "service stopped");
    host::WithdrawGameService(SC_CVARS_NAME);
    g_started = false;
}
}
