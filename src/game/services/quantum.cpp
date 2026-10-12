// Game-pack-owned bounded control state. Default bridge intentionally unavailable:
// signature matches alone do not verify drive layout, detour ABI or offline admission.
#include "quantum.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
namespace sco::game::services {
namespace {
bool started = false, held = false, busy = false, restart = false;
uint64_t session = 0;
uint32_t now = 0, heldSince = 0, holdMs = 0;
sco_plugin* owner = nullptr;
QuantumBackend bridge;
hook::Transaction hooks;
struct BridgeScope { BridgeScope() { busy = true; } ~BridgeScope() { busy = false; } };
sco_result Enter(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    if (!started || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    if (restart || busy) return SCO_FAILED;
    return SCO_OK;
}
void Fault() {
    restart = true;
    caps::Set("game.quantum.control", false, "quantum bridge fault; restart required");
}
bool Apply(bool enabled, bool holding) {
    if (!bridge.apply) return false;
    BridgeScope scope;
    try { if (bridge.apply(bridge.ctx, enabled, holding)) return true; } catch (...) {}
    Fault(); return false;
}
bool Clear() {
    if (!owner) return true;
    if (!Apply(false, false)) return false; // retain backing bridge/hooks after uncertain cleanup
    owner = nullptr; held = false; holdMs = 0;
    return true;
}
sco_result Observe(QuantumObservation& out) {
    if (restart) { out = {}; out.block = SC_QUANTUM_BLOCK_RESTART; return SCO_FAILED; }
    if (!bridge.observe || !bridge.apply) { out = {}; out.block = SC_QUANTUM_BLOCK_UNAVAILABLE; return SCO_UNAVAILABLE; }
    bool ok = false;
    { BridgeScope scope; try { ok = bridge.observe(bridge.ctx, &out); } catch (...) {} }
    if (!ok || out.phase > SC_QUANTUM_BOOSTING || out.block > SC_QUANTUM_BLOCK_RESTART || out.progress > SC_QUANTUM_PROGRESS_MAX) {
        Fault(); out = {}; out.block = SC_QUANTUM_BLOCK_RESTART; return SCO_FAILED;
    }
    if (!out.offline || !out.antiCheatAbsent) { out.block = SC_QUANTUM_BLOCK_OFFLINE; return SCO_UNAVAILABLE; }
    if (!out.drive) { out.block = SC_QUANTUM_BLOCK_DRIVE; return SCO_UNAVAILABLE; }
    return SCO_OK;
}
sco_result Enable(sco_plugin* self, uint32_t on) {
    if (auto r = Enter(self); r != SCO_OK) return r;
    if (on > 1) return SCO_BAD_ARG;
    if (owner && owner != self) return SCO_FAILED;
    if (!on) return Clear() ? SCO_OK : SCO_FAILED;
    QuantumObservation observation;
    if (auto r = Observe(observation); r != SCO_OK) { Clear(); return r; }
    if (owner) return SCO_OK;
    if (!Apply(true, false)) return SCO_FAILED;
    owner = self; return SCO_OK;
}
sco_result Hold(sco_plugin* self, uint32_t ms) {
    if (auto r = Enter(self); r != SCO_OK) return r;
    if (!ms || ms > SC_QUANTUM_MAX_HOLD_MS) return SCO_BAD_ARG;
    if (owner != self) return SCO_NOT_FOUND;
    QuantumObservation observation;
    if (auto r = Observe(observation); r != SCO_OK) { Clear(); return r; }
    if (observation.block != SC_QUANTUM_BLOCK_NONE) return SCO_UNAVAILABLE;
    if (!held && !Apply(true, true)) return SCO_FAILED;
    held = true; heldSince = now; holdMs = ms; return SCO_OK;
}
sco_result ReleaseControl(sco_plugin* self) {
    if (auto r = Enter(self); r != SCO_OK) return r;
    if (owner != self) return SCO_NOT_FOUND;
    if (held && !Apply(true, false)) return SCO_FAILED;
    held = false; holdMs = 0; return SCO_OK;
}
sco_result State(sco_plugin* self, sc_quantum_state* out) {
    if (auto r = Enter(self); r != SCO_OK && r != SCO_FAILED) return r;
    if (busy) return SCO_FAILED;
    if (!out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    QuantumObservation observation;
    const auto r = Observe(observation);
    *out = {sizeof(*out), owner ? 1u : 0u, held ? 1u : 0u,
            r == SCO_OK ? observation.phase : SC_QUANTUM_IDLE, observation.block,
            r == SCO_OK ? observation.progress : 0u, session};
    return r;
}
void OnTick(const char*, const void* data, void*) { if (data) TickQuantum(*static_cast<const uint32_t*>(data)); }
const sc_quantum_v1 table{sizeof(table), 0, Enable, Hold, ReleaseControl, State};
}
Result StartQuantum(const QuantumBackend& backend) {
    if (!OnGameThread()) return Result::WrongThread;
    if (restart || busy) return Result::Failed;
    if (started) return Result::Ok;
    if (backend.hookCount > 4 || (backend.hookCount && (!backend.hooks || !backend.observe || !backend.apply))) return Result::BadArg;
    // Detect aliased signature rows before touching code. Registry detects other owners.
    for (uint32_t i = 0; i < backend.hookCount; ++i) {
        const auto& h = backend.hooks[i];
        if (!h.target || !h.detour || !h.original) return Result::BadArg;
        if (hook::IsHooked(h.target)) return Result::Failed;
        for (uint32_t j = 0; j < i; ++j)
            if (h.target == backend.hooks[j].target) return Result::Failed;
    }
    if (backend.hookCount) {
        QuantumObservation admission;
        bool admitted = false;
        { BridgeScope scope; try { admitted = backend.observe(backend.ctx, &admission); } catch (...) {} }
        if (!admitted || !admission.offline || !admission.antiCheatAbsent) return Result::Unavailable;
        for (uint32_t i = 0; i < backend.hookCount; ++i) {
            const auto& h = backend.hooks[i]; hooks.Add(h.target, h.stolen, h.detour, h.original);
        }
        if (hooks.Commit() != hook::Error::None) {
            if (hooks.Rollback() != hook::Error::None) { bridge = backend; Fault(); }
            return Result::Failed;
        }
    }
    auto r = host::ProvideGameService(SC_QUANTUM_SERVICE_NAME, SC_QUANTUM_SERVICE_VERSION, &table);
    if (r != Result::Ok) { if (hooks.Rollback() != hook::Error::None) { bridge = backend; Fault(); } return r; }
    r = Subscribe(host::GameOwner(), "tick", OnTick, nullptr);
    if (r != Result::Ok) {
        host::WithdrawGameService(SC_QUANTUM_SERVICE_NAME);
        if (hooks.Rollback() != hook::Error::None) { bridge = backend; Fault(); }
        return r;
    }
    bridge = backend; started = true; now = 0; ++session;
    caps::Set("game.quantum.control", false, "engine bridge unverified");
    return Result::Ok;
}
void ReleaseQuantumOwner(const void* released) {
    if (OnGameThread() && released == owner && !busy && !restart) Clear();
}
void TickQuantum(uint32_t nowMs) {
    if (!OnGameThread() || !started || busy || restart) return;
    now = nowMs;
    if (!owner) return;
    QuantumObservation observation;
    if (detail::RuntimeStopping() || detail::Released(owner) || Observe(observation) != SCO_OK) { Clear(); return; }
    if (held && (now - heldSince >= holdMs || observation.block != SC_QUANTUM_BLOCK_NONE)) {
        if (Apply(true, false)) { held = false; holdMs = 0; }
    }
}
void StopQuantum() {
    if (!OnGameThread() || !started || busy) return;
    host::WithdrawGameService(SC_QUANTUM_SERVICE_NAME);
    caps::Set("game.quantum.control", false, "quantum service stopped");
    started = false;
    Unsubscribe(host::GameOwner(), "tick", OnTick);
    if (!restart && Clear()) {
        if (hooks.Rollback() == hook::Error::None) bridge = {};
        else Fault();
    }
}
}
