// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "mining.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "sco/game/mining.h"
#include "../../api/internal.h"
#include <atomic>
#include <algorithm>
#include <array>
namespace sco::game::services {
namespace {
std::atomic<bool> running{false}, hooked{false}, faulted{false}, hookAttempted{false};
std::atomic<uint32_t> requests{0};
std::atomic<uint64_t> cells{0}, promoted{0}, faults{0};
std::array<sco_plugin*, 64> owners{}; // game thread only; worker sees count, never owners
MiningBackend backend;
bool everStarted = false, busy = false;
void Fault() { faults.fetch_add(1); faulted.store(true); }
bool Admitted() {
    if (!backend.admitted) return false;
    try { return backend.admitted(); } catch (...) { Fault(); return false; }
}
sco_result Enter(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || sco::detail::Released(self)) return SCO_BAD_ARG;
    if (!running.load() || sco::detail::RuntimeStopping() || busy) return SCO_UNAVAILABLE;
    return SCO_OK;
}
bool Requested(sco_plugin* self) { return std::find(owners.begin(), owners.end(), self) != owners.end(); }
sco_result Request(sco_plugin* self, uint32_t on) {
    if (auto r = Enter(self); r != SCO_OK) return r;
    if (on > 1) return SCO_BAD_ARG;
    auto pos = std::find(owners.begin(), owners.end(), self);
    if (!on) {
        if (pos != owners.end()) { *pos = nullptr; requests.fetch_sub(1); }
        return SCO_OK; // disabling always works, including a restart latch
    }
    if (pos == owners.end()) {
        pos = std::find(owners.begin(), owners.end(), nullptr);
        if (pos == owners.end()) return SCO_FAILED;
        *pos = self; requests.fetch_add(1);
    }
    // Accept intent while unavailable; it must not be confused with active.
    if (!backend.ready || !backend.cleanupReady || faulted.load() || hooked.load()) return SCO_OK;
    busy = true;
    const bool admitted = Admitted();
    bool installed = false;
    if (admitted && !faulted.load() && backend.install) {
        hookAttempted.store(true); // a false/throwing installer can already have patched code
        try { installed = backend.install(); } catch (...) { Fault(); }
    }
    if (installed) hooked.store(true);
    busy = false;
    return SCO_OK;
}
sco_result State(sco_plugin* self, sc_mining_state* out) {
    if (auto r = Enter(self); r != SCO_OK) return r;
    if (!out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    const bool wanted = Requested(self);
    uint32_t block = SC_MINING_BLOCK_OFF;
    if (wanted) {
        if (faulted.load()) block = SC_MINING_BLOCK_RESTART;
        else if (!backend.ready || !backend.cleanupReady || !backend.install || !backend.read)
            block = SC_MINING_BLOCK_UNAVAILABLE;
        else {
            busy = true;
            const bool admitted = Admitted();
            busy = false;
            if (faulted.load()) block = SC_MINING_BLOCK_RESTART;
            else if (!admitted) block = SC_MINING_BLOCK_ADMISSION;
            else if (!hooked.load()) block = SC_MINING_BLOCK_UNAVAILABLE;
        }
    }
    *out = {sizeof(*out), wanted ? 1u : 0u,
            wanted && block == SC_MINING_BLOCK_OFF ? 1u : 0u, block,
            cells.load(), promoted.load(), faults.load()};
    return SCO_OK; // copied refusal state is a successful observation
}
const sc_mining_v1 table{sizeof(table), 0, Request, State};
}
bool MiningHookRetained() noexcept { return hookAttempted.load(); }
Result StartMining(bool cleanupReady) {
#ifdef _WIN32
    return StartMining(NativeMiningBackend(cleanupReady));
#else
    (void)cleanupReady;
    return StartMining(MiningBackend{});
#endif
}
Result StartMining(const MiningBackend& b) {
    if (!OnGameThread()) return Result::WrongThread;
    if (running.load()) return Result::Ok;
    if (everStarted || faulted.load()) return Result::Unavailable; // process-retained immutable callbacks
    auto r = host::ProvideGameService(SC_MINING_SERVICE_NAME, SC_MINING_SERVICE_VERSION, &table);
    if (r != Result::Ok) return r;
    backend = b;
    everStarted = true; running.store(true);
    caps::Set(SC_MINING_SERVICE_NAME, false, "off by default; native mining/admission must be verified");
    return Result::Ok;
}
void ReleaseMiningOwner(const void* released) {
    if (!OnGameThread() || !released) return;
    for (auto& owner : owners) if (owner == released) { owner = nullptr; requests.fetch_sub(1); }
}
void StopMining() {
    if (!OnGameThread() || !running.exchange(false)) return;
    requests.store(0); owners.fill(nullptr);
    host::WithdrawGameService(SC_MINING_SERVICE_NAME);
    caps::Set(SC_MINING_SERVICE_NAME, false, "mining stopped; retained pass-through hook");
    // Do NOT remove detour, trampoline or callbacks: native workers can be inside.
}
void MiningReadFault() { Fault(); }
uint32_t MiningCellFlags(uintptr_t builder, uintptr_t cell, uint32_t lod, uint32_t flags) {
    cells.fetch_add(1, std::memory_order_relaxed);
    if (flags != mining::kRuleFlags || lod > 1 || !builder || !cell ||
        !running.load() || !requests.load() || !hooked.load() || faulted.load()) return flags;
    if (!Admitted() || faulted.load()) return flags;
    MiningCell data;
    bool read = false;
    try { read = backend.read && backend.read(builder, cell, &data); } catch (...) {}
    if (!read) { Fault(); return flags; }
    if (data.builderType != 0 || data.spawnGate != 0 || data.harvestLod < 2 || data.harvestLod > 100)
        return flags;
    // Recheck intent and admission immediately before forwarding the promotion.
    if (!running.load() || !requests.load() || faulted.load() || !Admitted() || faulted.load()) return flags;
    promoted.fetch_add(1, std::memory_order_relaxed);
    return flags | mining::kRuleSpawnBit;
}
}
