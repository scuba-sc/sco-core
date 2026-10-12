#include "offline_missions.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#include <cstring>
#include <type_traits>
#include <string_view>
#include <charconv>
#ifdef _MSC_VER
#include <windows.h>
#endif

namespace sco::game::offline {
namespace {
Backend backend;
bool started = false, faulted = false, busy = false;
struct Ticket {
    const sco_plugin* owner = nullptr;
    uint64_t key = 0, session = 0, mission = 0;
    sc_mission_offer offer{};
    sco_result result = SCO_FAILED;
    bool revoked = false;
    uint32_t phase = 0, objective = 0, state = SC_MISSION_ACTIVE;
    bool effectFailed = false;
};
Ticket tickets[SC_MISSIONS_MAX_ACTIVE];
uint64_t ledgerSession = 0;
bool ownReleaseHook = false;
struct ScriptSlot {
    bool used = false;
    sc_mission_script script{};
    char data[SC_MISSIONS_SCRIPT_BYTES]{};
    sco_result result = SCO_FAILED;
};
ScriptSlot scripts[SC_MISSIONS_MAX_SCRIPTS];
void Session(uint64_t id) {
    if (ledgerSession != id) {
        for (auto& t : tickets) t = {};
        for (auto& s : scripts) s = {};
        ledgerSession = id;
    }
}

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
    Session(admission.session);
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
bool SameOffer(const sc_mission_offer& a, const sc_mission_offer& b) {
    return a.flags == b.flags && a.offer_id == b.offer_id && a.broker_id == b.broker_id &&
        a.generation == b.generation && a.phase_count == b.phase_count &&
        a.objectives_in_phase == b.objectives_in_phase && a.local_reward == b.local_reward;
}
sco_result Accept(sco_plugin* self, const sc_mission_offer* offer, uint64_t key, uint64_t* out) {
    Admission a;
    if (const auto r = Enter(self, a); r != SCO_OK) return r;
    if (!offer || offer->size < sizeof(*offer) || !out || !key || !ValidOffer(*offer)) return SCO_BAD_ARG;
    for (const auto& t : tickets) {
        if (t.owner != self || t.key != key) continue;
        if (!SameOffer(t.offer, *offer)) return SCO_BAD_ARG;
        if (t.revoked) return SCO_NOT_FOUND;
        if (t.result == SCO_OK) *out = t.mission;
        return t.result;
    }
    if (!backend.offer || !backend.accept) return SCO_UNAVAILABLE;
    Ticket* slot = nullptr;
    for (auto& t : tickets) if (!t.owner) { slot = &t; break; }
    if (!slot) return SCO_TOO_MANY;
    CallScope scope;
    bool found = false;
    for (uint32_t i = 0; i < SC_MISSIONS_MAX_OFFERS; ++i) {
        sc_mission_offer current{}; current.size = sizeof(current);
        auto read = [&] { return backend.offer(backend.ctx, i, &current); };
        const auto r = Call(read);
        if (r == SCO_NOT_FOUND) break;
        if (r != SCO_OK) return r;
        if (!ValidOffer(current)) return SCO_FAILED;
        if (SameOffer(current, *offer)) { found = true; break; }
    }
    if (!found) return SCO_NOT_FOUND;
    *slot = {};
    slot->owner = self; slot->key = key; slot->session = a.session; slot->offer = *offer;
    // Reserve before side effects; uncertain failures never trigger another attempt for this key.
    auto effect = [&] { return backend.accept(backend.ctx, &slot->offer, &slot->mission); };
    slot->result = Call(effect);
    if (slot->result == SCO_OK) {
        if (!slot->mission) { faulted = true; slot->result = SCO_CRASHED; }
        for (const auto& t : tickets)
            if (&t != slot && t.mission && t.mission == slot->mission) {
                faulted = true; slot->result = SCO_CRASHED;
            }
        if (slot->result == SCO_OK) *out = slot->mission;
    }
    return slot->result;
}
Ticket* Owned(sco_plugin* self, uint64_t id) {
    for (auto& t : tickets)
        if (t.owner == self && !t.revoked && t.mission == id && t.result == SCO_OK && t.session == ledgerSession)
            return &t;
    return nullptr;
}
sco_result CheckProgress(const Ticket& t) {
    if (!backend.status) return SCO_UNAVAILABLE;
    sc_mission_status v{}; v.size = sizeof(v);
    auto read = [&] { return backend.status(backend.ctx, t.mission, &v); };
    const auto r = Call(read);
    if (r != SCO_OK) return r;
    if (!ValidStatus(v, t.mission, t.session) || v.offer_id != t.offer.offer_id ||
        v.phase_count != t.offer.phase_count || v.objectives_in_phase != t.offer.objectives_in_phase ||
        v.phase != t.phase || v.objective != t.objective || v.state != t.state)
        return SCO_FAILED; // no unproved mission-kind/game state reconciliation
    return SCO_OK;
}
sco_result Objective(sco_plugin* self, uint64_t id, uint32_t phase, uint32_t objective) {
    Admission a;
    if (const auto r = Enter(self, a); r != SCO_OK) return r;
    if (!id || phase >= SC_MISSIONS_MAX_STEPS || objective >= SC_MISSIONS_MAX_STEPS) return SCO_BAD_ARG;
    Ticket* t = Owned(self, id);
    if (!t) return SCO_NOT_FOUND;
    if (phase >= t->offer.phase_count || objective >= t->offer.objectives_in_phase) return SCO_BAD_ARG;
    if (phase < t->phase || (phase == t->phase && objective < t->objective)) return SCO_OK;
    if (t->state != SC_MISSION_ACTIVE || t->effectFailed) return SCO_FAILED;
    if (phase != t->phase || objective != t->objective) return SCO_BAD_ARG;
    if (!backend.objective) return SCO_UNAVAILABLE;
    CallScope scope;
    if (const auto r = CheckProgress(*t); r != SCO_OK) return r;
    auto effect = [&] { return backend.objective(backend.ctx, id, phase, objective); };
    const auto r = Call(effect);
    if (r == SCO_OK) ++t->objective;
    else t->effectFailed = true; // never automatically replay an uncertain effect
    return r;
}
sco_result Advance(sco_plugin* self, uint64_t id, uint32_t phase) {
    Admission a;
    if (const auto r = Enter(self, a); r != SCO_OK) return r;
    if (!id || phase >= SC_MISSIONS_MAX_STEPS) return SCO_BAD_ARG;
    Ticket* t = Owned(self, id);
    if (!t) return SCO_NOT_FOUND;
    if (phase >= t->offer.phase_count) return SCO_BAD_ARG;
    if (phase < t->phase || (phase == t->phase && t->state == SC_MISSION_COMPLETED)) return SCO_OK;
    if (t->state != SC_MISSION_ACTIVE || t->effectFailed || phase != t->phase ||
        t->objective != t->offer.objectives_in_phase) return SCO_FAILED;
    if (!backend.advance) return SCO_UNAVAILABLE;
    CallScope scope;
    if (const auto r = CheckProgress(*t); r != SCO_OK) return r;
    auto effect = [&] { return backend.advance(backend.ctx, id, phase); };
    const auto r = Call(effect);
    if (r == SCO_OK) {
        if (phase + 1 == t->offer.phase_count) t->state = SC_MISSION_COMPLETED;
        else { ++t->phase; t->objective = 0; }
    } else t->effectFailed = true;
    return r;
}
sco_result Abandon(sco_plugin* self, uint64_t id) {
    Admission a;
    if (const auto r = Enter(self, a); r != SCO_OK) return r;
    if (!id) return SCO_BAD_ARG;
    Ticket* t = Owned(self, id);
    if (!t) return SCO_NOT_FOUND;
    if (t->state == SC_MISSION_ABANDONED) return SCO_OK;
    if (t->state == SC_MISSION_COMPLETED || t->effectFailed) return SCO_FAILED;
    if (!backend.abandon) return SCO_UNAVAILABLE;
    CallScope scope;
    auto effect = [&] { return backend.abandon(backend.ctx, id); };
    const auto r = Call(effect);
    if (r == SCO_OK) t->state = SC_MISSION_ABANDONED;
    else t->effectFailed = true;
    return r;
}
bool SafePath(const char (&path)[128]) {
    const auto end = static_cast<const char*>(std::memchr(path, 0, sizeof(path)));
    if (!end) return false;
    std::string_view p(path, static_cast<size_t>(end - path));
    if (!p.starts_with("missions/") || !p.ends_with(".scom")) return false;
    p.remove_prefix(9); p.remove_suffix(5);
    if (p.empty() || p.size() > 100) return false;
    for (char c : p) if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_') return false;
    if (p == "con" || p == "prn" || p == "aux" || p == "nul") return false;
    if (p.size() == 4 && (p.starts_with("com") || p.starts_with("lpt")) && p[3] >= '1' && p[3] <= '9') return false;
    return true;
}
bool Number(std::string_view& s, std::string_view key) {
    if (!s.starts_with(key)) return false;
    s.remove_prefix(key.size());
    const size_t n = s.find('\n');
    if (n == std::string_view::npos || !n || n > 2 || (n > 1 && s[0] == '0')) return false;
    uint32_t value = 0;
    auto result = std::from_chars(s.data(), s.data() + n, value);
    if (result.ec != std::errc{} || result.ptr != s.data() + n || !value || value > SC_MISSIONS_MAX_STEPS) return false;
    s.remove_prefix(n + 1);
    return true;
}
bool ValidScript(const sc_mission_script& v) {
    if (v.size < sizeof(v) || v.flags || v._pad || !SafePath(v.relative_path) ||
        !v.bytes || !v.byte_count || v.byte_count > SC_MISSIONS_SCRIPT_BYTES) return false;
    std::string_view s(v.bytes, v.byte_count);
    constexpr std::string_view magic = "sco-mission 1\n";
    if (!s.starts_with(magic)) return false;
    s.remove_prefix(magic.size());
    return Number(s, "phases ") && Number(s, "objectives ") && s.empty();
}
sco_result Ingest(sco_plugin* self, const sc_mission_script* input) {
    Admission a;
    if (const auto r = Enter(self, a); r != SCO_OK) return r;
    if (!input || input->size < sizeof(*input) || !ValidScript(*input)) return SCO_BAD_ARG;
    if (!backend.ingest) return SCO_UNAVAILABLE;
    ScriptSlot* slot = nullptr;
    for (auto& s : scripts) {
        if (!s.used) { if (!slot) slot = &s; continue; }
        if (std::strcmp(input->relative_path, s.script.relative_path)) continue;
        if (s.script.byte_count != input->byte_count || std::memcmp(s.data, input->bytes, input->byte_count))
            return SCO_FAILED; // library registration immutable for session; restart to edit
        return s.result;
    }
    if (!slot) return SCO_TOO_MANY;
    slot->used = true; slot->script = *input; slot->script.size = sizeof(slot->script);
    std::memcpy(slot->data, input->bytes, input->byte_count); slot->script.bytes = slot->data;
    CallScope scope;
    auto effect = [&] { return backend.ingest(backend.ctx, &slot->script); };
    slot->result = Call(effect);
    return slot->result;
}
const sc_missions_v1 table{sizeof(table), 0, Offer, Status, Accept, Objective, Advance, Abandon, Ingest};
}
void ReleaseOwner(const void* owner) {
    if (!OnGameThread() || !started || !owner) return;
    if (busy) { faulted = true; return; } // cannot safely free/cancel an effect's callback context
    Admission a;
    CallScope scope;
    bool admitted = false;
    if (!faulted && backend.admission) {
        auto read = [&] { a = backend.admission(backend.ctx); return SCO_OK; };
        admitted = Call(read) == SCO_OK && a.session == ledgerSession && a.offline &&
            a.eac_absent && !a.shutting_down;
    }
    for (auto& t : tickets) {
        if (t.owner != owner || t.revoked) continue;
        t.revoked = true;
        if (!admitted || faulted || !backend.abandon || t.result != SCO_OK ||
            t.state != SC_MISSION_ACTIVE || t.effectFailed) continue;
        // Revoke first, attempt once, never replay failed/uncertain cleanup.
        auto effect = [&] { return backend.abandon(backend.ctx, t.mission); };
        if (Call(effect) == SCO_OK) t.state = SC_MISSION_ABANDONED;
        else t.effectFailed = true;
    }
}
Result Start(const Backend& b, bool shared_release_hook) {
    if (!OnGameThread()) return Result::WrongThread;
    if (faulted) return Result::Crashed;
    if (started) return Result::BadArg;
    if (b.accept && !b.abandon) return Result::BadArg;
    if (b.accept && !shared_release_hook) {
        const auto hook = AddReleaseHook(ReleaseOwner);
        if (hook != Result::Ok) return hook;
        ownReleaseHook = true;
    }
    const auto r = host::ProvideGameService(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION, &table);
    if (r != Result::Ok) {
        if (ownReleaseHook) RemoveReleaseHook(ReleaseOwner);
        ownReleaseHook = false; return r;
    }
    backend = b; started = true;
    caps::Set("game.missions.read", b.admission && b.offer && b.status, "validated offline backend required");
    caps::Set("game.missions.accept", b.admission && b.offer && b.accept, "validated offline backend required");
    caps::Set("game.missions.progress", b.admission && b.status && b.objective && b.advance, "validated offline backend required");
    caps::Set("game.missions.scripts", b.admission && b.ingest, "validated offline backend required");
    return Result::Ok;
}
Result Stop() {
    if (!OnGameThread()) return Result::WrongThread;
    if (busy) return Result::Failed;
    if (!started) return Result::Ok;
    if (ownReleaseHook) RemoveReleaseHook(ReleaseOwner);
    ownReleaseHook = false;
    started = false;
    host::WithdrawGameService(SC_MISSIONS_SERVICE_NAME);
    caps::Set("game.missions.read", false, "stopped");
    caps::Set("game.missions.accept", false, "stopped");
    caps::Set("game.missions.progress", false, "stopped");
    caps::Set("game.missions.scripts", false, "stopped");
    backend = {};
    return Result::Ok;
}
}
