// SPDX-License-Identifier: GPL-3.0-only
#include "loadout.h"
#include "sc_loadout.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#include <cstring>
#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>
#endif
namespace sco::game::services {
namespace {
bool started = false;
LoadoutBackend engine{};
bool faulted = false;
bool applying = false;
LoadoutAsyncBackend asyncEngine{};
struct Job {
    sco_plugin* owner = nullptr;
    uint64_t id = 0, session = 0, token = 0;
    uint32_t since = 0;
    bool timed = false;
    sc_loadout_item items[SC_LOADOUT_MAX_ITEMS]{};
    sc_loadout_job_report result{};
};
Job jobs[SC_LOADOUT_MAX_JOBS]{};
uint64_t nextJob = 0;
uint32_t tickNow = 0;
bool haveTick = false;
struct ApplyScope {
    ApplyScope() { applying = true; }
    ~ApplyScope() { applying = false; }
};
int OfflineCpp() {
    try { return engine.offline() ? 1 : 0; } catch (...) { return -1; }
}
sc_loadout_status EquipCpp(uint32_t slot, const char* name) {
    try { return engine.equip(slot, name); } catch (...) { return SC_LOADOUT_STATUS_FORCE32; }
}
int SafeOffline() {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return OfflineCpp(); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
#else
    return OfflineCpp();
#endif
}
sc_loadout_status SafeEquip(uint32_t slot, const char* name) {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return EquipCpp(slot, name); } __except (EXCEPTION_EXECUTE_HANDLER) { return SC_LOADOUT_STATUS_FORCE32; }
#else
    return EquipCpp(slot, name);
#endif
}
void Fault() {
    faulted = true;
    caps::Set("game.loadout.apply", false, "loadout provider fault; process restart required");
    caps::Set("game.loadout.weapons", false, "loadout provider fault; process restart required");
    caps::Set("game.loadout.async", false, "loadout provider fault; process restart required");
}
using AsyncThunk = void(*)(void*);
bool AsyncCpp(AsyncThunk fn, void* data) {
    try { fn(data); return true; } catch (...) { return false; }
}
bool AsyncGuard(AsyncThunk fn, void* data) {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return AsyncCpp(fn, data); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return AsyncCpp(fn, data);
#endif
}
template <typename F> bool CallAsync(F fn) {
    const ApplyScope scope;
    if (AsyncGuard([](void* data) { (*static_cast<F*>(data))(); }, &fn)) return true;
    Fault(); return false;
}
bool InstalledAsync() {
    return asyncEngine.verified && asyncEngine.admit && asyncEngine.begin &&
           asyncEngine.poll && asyncEngine.discard;
}
bool Live(sco_plugin* self) { return host::PluginId(self) && !detail::Released(self); }
sco_result CheckRequest(const sc_loadout_request* request) {
    if (!request || request->size < sizeof(*request) || !request->count || !request->items ||
        request->item_stride != sizeof(sc_loadout_item) || request->_pad) return SCO_BAD_ARG;
    if (request->count > SC_LOADOUT_MAX_ITEMS) return SCO_TOO_MANY;
    for (uint32_t i = 0; i < request->count; ++i) {
        const auto& item = request->items[i];
        if (item._pad || !item.item_class[0]) return SCO_BAD_ARG;
        uint32_t n = 0;
        for (; n < SC_LOADOUT_ITEM_NAME_BYTES && item.item_class[n]; ++n) {
            const char c = item.item_class[n];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) return SCO_BAD_ARG;
        }
        if (n == SC_LOADOUT_ITEM_NAME_BYTES) return SCO_BAD_ARG;
        for (uint32_t j = 0; j < i; ++j)
            if (request->items[j].slot == item.slot) return SCO_BAD_ARG;
    }
    return SCO_OK;
}
bool Discard(Job& job) {
    if (!job.token) return true;
    if (faulted) return false; // retain uncertain native resources until process exit
    const uint64_t token = job.token;
    if (!CallAsync([&] { asyncEngine.discard(token); })) return false;
    job.token = 0;
    return true;
}
sco_result Submit(sco_plugin* self, const sc_loadout_request* request, uint64_t* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!Live(self) || !out) return SCO_BAD_ARG;
    const auto checked = CheckRequest(request);
    if (checked != SCO_OK) return checked;
    if (!started || faulted || applying || detail::RuntimeStopping() || !InstalledAsync() ||
        !caps::Has("game.loadout.async")) return SCO_UNAVAILABLE;
    Job* free = nullptr;
    for (auto& job : jobs) {
        if (job.owner == self) return SCO_TOO_MANY;
        if (!job.owner && !free) free = &job;
    }
    if (!free || nextJob == UINT64_MAX) return SCO_TOO_MANY;
    uint64_t session = 0;
    bool admitted = false;
    if (!CallAsync([&] { admitted = asyncEngine.admit(&session); })) return SCO_FAILED;
    if (!admitted || !session) return SCO_UNAVAILABLE;
    *free = {};
    free->owner = self; free->id = ++nextJob; free->session = session;
    free->since = tickNow; free->timed = haveTick;
    free->result.size = sizeof(free->result);
    free->result.state = SC_LOADOUT_JOB_PENDING;
    free->result.report.size = sizeof(sc_loadout_report);
    free->result.report.count = request->count;
    for (uint32_t i = 0; i < request->count; ++i) {
        free->items[i] = request->items[i];
        free->result.report.slots[i] = {request->items[i].slot, SC_LOADOUT_UNAVAILABLE};
    }
    *out = free->id;
    return SCO_OK;
}
sco_result Status(sco_plugin* self, uint64_t id, sc_loadout_job_report* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!Live(self) || !out || out->size < sizeof(*out) || !id) return SCO_BAD_ARG;
    if (!started || applying) return SCO_UNAVAILABLE;
    for (const auto& job : jobs) if (job.owner == self && job.id == id) {
        *out = job.result; return SCO_OK;
    }
    return SCO_NOT_FOUND;
}
sco_result Cancel(sco_plugin* self, uint64_t id) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!Live(self) || !id) return SCO_BAD_ARG;
    if (!started || applying) return SCO_UNAVAILABLE;
    for (auto& job : jobs) if (job.owner == self && job.id == id) {
        if (!Discard(job)) return SCO_FAILED;
        job = {}; return SCO_OK;
    }
    return SCO_NOT_FOUND;
}
sco_result Apply(sco_plugin* self, const sc_loadout_request* request, sc_loadout_report* report) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!host::PluginId(self) || detail::Released(self) || !request || !report ||
        request->size < sizeof(*request) || report->size < sizeof(*report) ||
        !request->count || !request->items || request->item_stride != sizeof(sc_loadout_item) || request->_pad)
        return SCO_BAD_ARG;
    if (request->count > SC_LOADOUT_MAX_ITEMS) return SCO_TOO_MANY;
    const uint32_t count = request->count;
    sc_loadout_item items[SC_LOADOUT_MAX_ITEMS]{};
    for (uint32_t i = 0; i < count; ++i) {
        items[i] = request->items[i];
        const auto& item = items[i];
        if (item._pad || !item.item_class[0]) return SCO_BAD_ARG;
        uint32_t n = 0;
        for (; n < SC_LOADOUT_ITEM_NAME_BYTES && item.item_class[n]; ++n) {
            const char c = item.item_class[n];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) return SCO_BAD_ARG;
        }
        if (n == SC_LOADOUT_ITEM_NAME_BYTES) return SCO_BAD_ARG;
        for (uint32_t j = 0; j < i; ++j)
            if (items[j].slot == item.slot) return SCO_BAD_ARG;
    }
    *report = {};
    report->size = sizeof(*report); report->count = count;
    for (uint32_t i = 0; i < count; ++i)
        report->slots[i] = {items[i].slot, SC_LOADOUT_UNAVAILABLE};
    if (!started || faulted || applying || !caps::Has("game.loadout.apply") || !engine.verified || !engine.offline || !engine.equip)
        return SCO_UNAVAILABLE;
    const ApplyScope scope;
    const int admitted = SafeOffline();
    if (admitted < 0) { Fault(); return SCO_FAILED; }
    if (!admitted) return SCO_UNAVAILABLE;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& item = items[i];
        auto& result = report->slots[i];
        if (item.slot < SC_LOADOUT_SLOT_HEAD || item.slot > SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY) {
            result.status = SC_LOADOUT_UNSUPPORTED;
            continue;
        }
        if (item.slot >= SC_LOADOUT_SLOT_PRIMARY) {
            if (!engine.weapons) { result.status = SC_LOADOUT_UNSUPPORTED; continue; }
            if (!caps::Has("game.loadout.weapons")) continue;  // stays UNAVAILABLE, no engine call
        }
        if (i) {
            const int stillAdmitted = SafeOffline();
            if (stillAdmitted < 0) { result.status = SC_LOADOUT_FAILED; Fault(); break; }
            if (!stillAdmitted || !caps::Has("game.loadout.apply")) break;
        }
        result.status = SafeEquip(item.slot, item.item_class);
        if (result.status < SC_LOADOUT_APPLIED || result.status > SC_LOADOUT_UNAVAILABLE) {
            result.status = SC_LOADOUT_FAILED;
            Fault(); break;
        }
        if (result.status == SC_LOADOUT_APPLIED) ++report->applied;
    }
    return report->applied == report->count ? SCO_OK : SCO_FAILED;
}
void OnTick(const char*, const void* data, void*) { if (data) TickLoadout(*static_cast<const uint32_t*>(data)); }
const sc_loadout_v1_2 table = {sizeof(table), 0, Apply, Submit, Status, Cancel};
}
Result StartLoadout(const LoadoutBackend* backend, const LoadoutAsyncBackend* async) {
    if (!OnGameThread()) return Result::WrongThread;
    if (applying) return Result::Unavailable;
    if (faulted) return Result::Failed;
    if (started) return Result::Ok;
    engine = backend ? *backend : LoadoutBackend{};
    asyncEngine = async ? *async : LoadoutAsyncBackend{};
    Result r = host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_2, &table);
    if (r == Result::Ok) {
        r = Subscribe(host::GameOwner(), "tick", OnTick, nullptr);
        if (r != Result::Ok) host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    }
    started = r == Result::Ok;
    haveTick = false;
    const bool applyOn = started && !faulted && engine.verified && engine.equip && engine.offline;
    caps::Set("game.loadout.apply", applyOn,
              faulted ? "loadout provider fault; process restart required" :
                        "typed engine equip methods not verified; no provider installed");
    caps::Set("game.loadout.weapons", applyOn && engine.weapons,
              faulted ? "loadout provider fault; process restart required" :
                        "typed weapon and multitool slot methods not verified; default off");
    caps::Set("game.loadout.async", started && !faulted && InstalledAsync(),
              "verified async spawn, attachment and readback provider required");
    if (!started) { engine = {}; asyncEngine = {}; }
    return r;
}
void TickLoadout(uint32_t now) {
    if (!OnGameThread() || !started || applying) return;
    tickNow = now; haveTick = true;
    for (auto& job : jobs) {
        if (!job.owner || job.result.state != SC_LOADOUT_JOB_PENDING) continue;
        if (!job.timed) { job.since = now; job.timed = true; }
        uint64_t session = 0;
        bool admitted = false;
        if (faulted || !Live(job.owner) || detail::RuntimeStopping() || !caps::Has("game.loadout.async") ||
            !CallAsync([&] { admitted = asyncEngine.admit(&session); }) || !admitted ||
            !session || session != job.session || now - job.since >= SC_LOADOUT_TIMEOUT_MS) {
            Discard(job); job.result.state = SC_LOADOUT_JOB_FAILED; continue;
        }
        const uint32_t index = job.result.completed;
        auto& result = job.result.report.slots[index];
        bool complete = false, ok = false;
        sc_loadout_status terminal = SC_LOADOUT_UNAVAILABLE;
        if (!job.token) {
            const auto& item = job.items[index];
            if (item.slot < SC_LOADOUT_SLOT_HEAD || item.slot > SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY) {
                complete = true; ok = true; terminal = SC_LOADOUT_UNSUPPORTED;
            } else {
                if (!CallAsync([&] { ok = asyncEngine.begin(item.slot, item.item_class, &job.token, &terminal); }))
                    ok = false;
                complete = !job.token;
            }
        } else {
            if (!CallAsync([&] { ok = asyncEngine.poll(job.token, &complete, &terminal); })) ok = false;
        }
        if (!ok || (complete && (terminal < SC_LOADOUT_APPLIED || terminal > SC_LOADOUT_UNAVAILABLE))) {
            Fault(); job.result.state = SC_LOADOUT_JOB_FAILED; continue;
        }
        if (!complete) continue;
        if (!Discard(job)) { job.result.state = SC_LOADOUT_JOB_FAILED; continue; }
        result.status = terminal;
        if (terminal == SC_LOADOUT_APPLIED) ++job.result.report.applied;
        if (++job.result.completed == job.result.report.count) job.result.state = SC_LOADOUT_JOB_COMPLETE;
    }
}
void ReleaseLoadoutOwner(const void* owner) {
    if (!OnGameThread() || applying) return;
    for (auto& job : jobs) if (job.owner == owner && Discard(job)) job = {};
}
void StopLoadout() {
    if (!OnGameThread() || !started || applying) return;
    host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    caps::Set("game.loadout.apply", false, "game loadout stopped");
    caps::Set("game.loadout.weapons", false, "game loadout stopped");
    caps::Set("game.loadout.async", false, "game loadout stopped");
    Unsubscribe(host::GameOwner(), "tick", OnTick);
    for (auto& job : jobs) if (job.owner && Discard(job)) job = {};
    started = false;
    engine = {};
    if (!faulted) asyncEngine = {};
}
}
