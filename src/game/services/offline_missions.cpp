#include "offline_missions.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#include <cstring>
#include <type_traits>
#ifdef _MSC_VER
#include <windows.h>
#endif

namespace sco::game::offline {
namespace {
Backend backend;
bool started = false, faulted = false, busy = false;

// This frame contains no C++ objects needing unwinding (MSVC C2712).
sco_result NativeCall(sco_result (*fn)(void*), void* ctx) {
#ifdef _MSC_VER
    __try { return fn(ctx); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return SCO_CRASHED; }
#else
    return fn(ctx);
#endif
}
template<class F> sco_result Call(F&& fn) {
    sco_result r;
    try {
        r = NativeCall([](void* p) -> sco_result { return (*static_cast<std::remove_reference_t<F>*>(p))(); }, &fn);
    } catch (...) { r = SCO_CRASHED; }
    if (r == SCO_CRASHED) faulted = true; // never clear: restart required
    return r;
}
struct CallScope { CallScope() { busy = true; } ~CallScope() { busy = false; } };
sco_result Enter(sco_plugin* self, Admission& admission) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    if (faulted) return SCO_CRASHED;
    if (!started || !backend.admission) return SCO_UNAVAILABLE;
    if (busy) return SCO_FAILED;
    CallScope scope;
    auto read = [&] { admission = backend.admission(backend.ctx); return SCO_OK; };
    const sco_result r = Call(read);
    if (r != SCO_OK) return r;
    if (!admission.session || !admission.offline || !admission.eac_absent || admission.shutting_down)
        return SCO_UNAVAILABLE;
    return SCO_OK;
}
bool ValidOffer(const sc_mission_offer& v) {
    return v.size >= sizeof(v) && !v.flags && v.offer_id && v.broker_id && v.generation &&
        v.phase_count && v.phase_count <= SC_MISSIONS_MAX_STEPS &&
        v.objectives_in_phase && v.objectives_in_phase <= SC_MISSIONS_MAX_STEPS &&
        v.local_reward >= 0 && v.local_reward < 1000000000000LL;
}
bool ValidStatus(const sc_mission_status& v, uint64_t id, uint64_t session) {
    return v.size >= sizeof(v) && v.mission_id == id && v.offer_id && v.session == session &&
        v.state >= SC_MISSION_ACTIVE && v.state <= SC_MISSION_ABANDONED &&
        v.phase_count && v.phase_count <= SC_MISSIONS_MAX_STEPS && v.phase < v.phase_count &&
        v.objectives_in_phase && v.objectives_in_phase <= SC_MISSIONS_MAX_STEPS &&
        v.objective <= v.objectives_in_phase;
}
sco_result Offer(sco_plugin* self, uint32_t index, sc_mission_offer* out) {
    Admission a;
    if (const auto r = Enter(self, a); r != SCO_OK) return r;
    if (!out || out->size < sizeof(*out) || index >= SC_MISSIONS_MAX_OFFERS) return SCO_BAD_ARG;
    if (!backend.offer) return SCO_UNAVAILABLE;
    CallScope scope;
    sc_mission_offer v{}; v.size = sizeof(v);
    auto read = [&] { return backend.offer(backend.ctx, index, &v); };
    const auto r = Call(read);
    if (r != SCO_OK) return r;
    if (!ValidOffer(v)) return SCO_FAILED;
    const uint32_t size = out->size; *out = v; out->size = size;
    return SCO_OK;
}
sco_result Status(sco_plugin* self, uint64_t id, sc_mission_status* out) {
    Admission a;
    if (const auto r = Enter(self, a); r != SCO_OK) return r;
    if (!out || out->size < sizeof(*out) || !id) return SCO_BAD_ARG;
    if (!backend.status) return SCO_UNAVAILABLE;
    CallScope scope;
    sc_mission_status v{}; v.size = sizeof(v);
    auto read = [&] { return backend.status(backend.ctx, id, &v); };
    const auto r = Call(read);
    if (r != SCO_OK) return r;
    if (!ValidStatus(v, id, a.session)) return SCO_FAILED;
    const uint32_t size = out->size; *out = v; out->size = size;
    return SCO_OK;
}
const sc_missions_v1 table{sizeof(table), 0, Offer, Status};
}
Result Start(const Backend& b) {
    if (!OnGameThread()) return Result::WrongThread;
    if (faulted) return Result::Crashed;
    if (started) return Result::BadArg;
    const auto r = host::ProvideGameService(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION, &table);
    if (r != Result::Ok) return r;
    backend = b; started = true;
    caps::Set("game.missions.read", b.admission && b.offer && b.status, "validated offline backend required");
    return Result::Ok;
}
Result Stop() {
    if (!OnGameThread()) return Result::WrongThread;
    if (busy) return Result::Failed;
    if (!started) return Result::Ok;
    started = false;
    host::WithdrawGameService(SC_MISSIONS_SERVICE_NAME);
    caps::Set("game.missions.read", false, "stopped");
    backend = {};
    return Result::Ok;
}
}
