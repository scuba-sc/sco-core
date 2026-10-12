// SPDX-License-Identifier: GPL-3.0-only
#include "cvars.h"
#include "spawn.h"
#include <sc_cvars_write.h>
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#include <cstring>
namespace sco::game::services {
namespace {
CvarsPort g_port;
bool g_started = false;
bool g_session = false;
bool g_calling = false;
bool g_faulted = false; // process-lifetime: never reset an untrusted adapter
struct CallScope { CallScope() { g_calling = true; } ~CallScope() { g_calling = false; } };
constexpr const char* kNames[] = {
    "i_target_selector.targeting2_enabled", "pl_lensdisplay.minimap_enabled",
    "pl_lensdisplay.greebles_enabled", "g_squadron_frontend"
};
void LatchFault() {
    g_faulted = true;
    caps::Set("game.cvars.read", false, "restart required");
    caps::Set("game.cvars.write", false, "restart required");
}
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
    if (!g_started || !g_session || g_faulted || detail::RuntimeStopping() || !g_port.verified || !g_port.offline_admitted ||
        !g_port.read_bool || !caps::Has("game.cvars.read")) return SCO_UNAVAILABLE;
    if (g_calling) return SCO_FAILED;
    CallScope scope;
    uint32_t value = 0;
    const sco_result r = g_port.read_bool(g_port.ctx, static_cast<uint32_t>(index), &value);
    if (r == SCO_CRASHED) LatchFault();
    if (r != SCO_OK) return r;
    if (value > 1) return SCO_FAILED;
    *out = value;
    return SCO_OK;
}
const sc_cvars_v1 kTable{sizeof(sc_cvars_v1), 0, Get};
struct Lease { sco_plugin* owner = nullptr; uint32_t prior = 0; };
Lease g_leases[4];
sco_result Set(sco_plugin* self, const char* name, uint32_t type, uint32_t value) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    const int index = Index(name);
    if (index < 0 || type != SC_CVAR_BOOL || value > 1) return SCO_BAD_ARG;
    if (!g_started || !g_session || g_faulted || detail::RuntimeStopping() || !g_port.verified ||
        !g_port.offline_admitted || !g_port.write_verified || !g_port.write_bool || !SpawnReleaseHooked() ||
        !g_port.read_bool || !caps::Has("game.cvars.read") || !caps::Has("game.cvars.write")) return SCO_UNAVAILABLE;
    if (g_calling) return SCO_FAILED;
    auto& lease = g_leases[index];
    if (lease.owner && lease.owner != self) return SCO_UNAVAILABLE;
    if (!lease.owner) {
        uint32_t prior = 0;
        const auto r = Get(self, name, type, &prior);
        if (r != SCO_OK) return r;
        if (detail::Released(self)) return SCO_BAD_ARG;
        if (g_faulted || !g_session || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
        lease = {self, prior};
    }
    CallScope scope;
    const auto r = g_port.write_bool(g_port.ctx, static_cast<uint32_t>(index), value);
    if (r != SCO_OK) { LatchFault(); return r; }
    if (g_faulted) return SCO_FAILED;
    uint32_t observed = 2;
    const auto read = g_port.read_bool(g_port.ctx, static_cast<uint32_t>(index), &observed);
    if (read != SCO_OK || observed != value || g_faulted) {
        LatchFault(); return read != SCO_OK ? read : SCO_FAILED;
    }
    return SCO_OK;
}
sco_result RestoreOwned(const void* owner) {
    bool found = false;
    for (const auto& lease : g_leases) if (lease.owner && (!owner || lease.owner == owner)) found = true;
    if (!found) return SCO_OK;
    if (g_faulted) return SCO_UNAVAILABLE;
    if (g_calling) { LatchFault(); return SCO_FAILED; }
    CallScope scope;
    for (uint32_t i = 0; i != 4; ++i) if (g_leases[i].owner && (!owner || g_leases[i].owner == owner)) {
        const auto r = g_port.write_bool(g_port.ctx, i, g_leases[i].prior);
        if (r != SCO_OK) { LatchFault(); return r; }
        if (g_faulted) return SCO_FAILED;
        uint32_t observed = 2;
        const auto read = g_port.read_bool(g_port.ctx, i, &observed);
        if (read != SCO_OK || observed != g_leases[i].prior) {
            LatchFault(); return read != SCO_OK ? read : SCO_FAILED;
        }
        g_leases[i] = {};
    }
    return SCO_OK;
}
sco_result Restore(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    if (!g_started || !g_session || g_faulted || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    return RestoreOwned(self);
}
const sc_cvars_write_v1 kWrite{sizeof(sc_cvars_write_v1), 0, Set, Restore};
void OnSessionExit(const char*, const void*, void*) {
    RestoreOwned(nullptr);
    g_session = false;
    caps::Set("game.cvars.read", false, "session ended");
    caps::Set("game.cvars.write", false, "session ended");
}
}
uint32_t PendingCvarRestores() {
    if (!OnGameThread()) return 0;
    uint32_t count = 0;
    for (const auto& lease : g_leases) if (lease.owner) ++count;
    return count;
}
void ReleaseCvarsOwner(const void* owner) {
    if (OnGameThread() && owner && !g_faulted) RestoreOwned(owner);
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
    caps::Set("game.cvars.write", false, "adapter unverified or service not published");
    const Result r = host::ProvideGameService(SC_CVARS_NAME, SC_CVARS_VERSION, &kTable);
    if (r != Result::Ok) return r;
    const Result w = host::ProvideGameService(SC_CVARS_WRITE_NAME, SC_CVARS_WRITE_VERSION, &kWrite);
    if (w != Result::Ok) { host::WithdrawGameService(SC_CVARS_NAME); return w; }
    caps::Set("game.cvars.write", g_port.verified && g_port.offline_admitted && g_port.read_bool &&
              g_port.write_verified && g_port.write_bool && SpawnReleaseHooked(), "verified offline write adapter and cleanup required");
    const Result subscribed = Subscribe(host::GameOwner(), "game.exit", OnSessionExit, nullptr);
    if (subscribed != Result::Ok) {
        host::WithdrawGameService(SC_CVARS_WRITE_NAME);
        host::WithdrawGameService(SC_CVARS_NAME);
        caps::Set("game.cvars.write", false, "session subscription unavailable");
        return subscribed;
    }
    g_started = true;
    g_session = true;
    caps::Set("game.cvars.read", g_port.verified && g_port.offline_admitted && g_port.read_bool,
              "verified offline adapter required");
    return Result::Ok;
}
Result StopCvars() {
    if (!OnGameThread()) return Result::WrongThread;
    if (g_calling) return Result::Failed;
    if (!g_started) return g_faulted ? Result::Failed : Result::Ok;
    OnSessionExit(nullptr, nullptr, nullptr);
    Unsubscribe(host::GameOwner(), "game.exit", OnSessionExit);
    caps::Set("game.cvars.read", false, "service stopped");
    caps::Set("game.cvars.write", false, "service stopped");
    host::WithdrawGameService(SC_CVARS_WRITE_NAME);
    host::WithdrawGameService(SC_CVARS_NAME);
    g_started = false;
    return g_faulted ? Result::Failed : Result::Ok;
}
}
