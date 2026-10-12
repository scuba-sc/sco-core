#pragma once
#include "sco/runtime.h"
#include "sco/hook.h"
#include <sc_quantum.h>
namespace sco::game::services {
// Private game-pack bridge; NOT shipped plugin SDK. No verified engine bridge exists yet.
// Fixtures inject copied observations. Production Start uses the empty bridge and fails closed.
struct QuantumObservation {
    bool offline = false, antiCheatAbsent = false, drive = false;
    uint32_t phase = SC_QUANTUM_IDLE, block = SC_QUANTUM_BLOCK_NONE, progress = 0;
};
struct QuantumHook { void* target; size_t stolen; void* detour; void** original; };
struct QuantumBackend {
    void* ctx = nullptr;
    bool (*observe)(void*, QuantumObservation*) = nullptr;
    bool (*apply)(void*, bool enabled, bool held) = nullptr;
    const QuantumHook* hooks = nullptr;
    uint32_t hookCount = 0; // bounded to four; copied at Start, never plugin-owned
};
Result StartQuantum(const QuantumBackend& backend = {});
void TickQuantum(uint32_t nowMs);
void StopQuantum();
// Forwarded by spawn.cpp's one game-pack runtime release hook (no extra kernel slot).
void ReleaseQuantumOwner(const void* owner);
}
