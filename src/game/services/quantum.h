#pragma once
#include "sco/runtime.h"
#include "sco/hook.h"
#include <sc_quantum.h>
namespace sco::game::services {
// Private host-source integration contract; NOT a plugin SDK or signature-only adapter.
// A host supplies this only after grounding local-drive ownership, the exact build's
// layouts/call ABI and reversible input/charge/audio/effect teardown. Unknown admission
// stays false. The default production configuration is empty and cannot mutate.
struct QuantumObservation {
    bool offline = false, antiCheatAbsent = false, drive = false;
    uint32_t phase = SC_QUANTUM_IDLE, block = SC_QUANTUM_BLOCK_NONE, progress = 0;
};
struct QuantumHook { void* target; size_t stolen; void* detour; void** original; };
struct QuantumBackend {
    void* ctx = nullptr;
    bool (*observe)(void*, QuantumObservation*) = nullptr;
    // Only bounded bool controls reach the bridge. It must save first-prior state
    // BEFORE writing, restrict writes to the local player's owned drive, validate
    // every engine value (including finite charge 0..1 and input 0/1), and restore
    // only its own changed state on (false,false), even after admission loss.
    // false/throw means uncertain partial mutation: no later callback is permitted.
    bool (*apply)(void*, bool enabled, bool held) = nullptr;
    const QuantumHook* hooks = nullptr;
    uint32_t hookCount = 0; // bounded to four; copied into the transaction at Start
};
// Serialized host-only setup, before production start; callbacks/context remain alive
// through stop, and through process exit after a fault. Successful stop clears the
// selection, including after a failed non-faulted start; hosts must configure it again
// for another lifecycle. Stop must run on the game thread outside a callback.
Result ConfigureProductionQuantum(const QuantumBackend& backend);
// Default selection never replaces an explicit host selection.
Result ConfigureImplicitQuantum(const QuantumBackend& backend);
Result StartProductionQuantum(bool ownerCleanupReady);
Result StartProductionQuantumWithDefault(bool ownerCleanupReady, Result (*configureDefault)());
// Windows game pack only (quantum_engine.cpp): configures the default production backend under the
// amended rule when every row resolves and fresh offline admission is positive right now. Otherwise
// it leaves the empty configuration (capability OFF, no hook) and returns Unavailable.
Result ConfigureDefaultProductionQuantum();
// Standalone host fixtures use the same provider, but must install the owner release
// forwarder themselves before passing ownerCleanupReady=true. No extra kernel slot.
Result StartQuantum(const QuantumBackend& backend = {}, bool ownerCleanupReady = false);
void TickQuantum(uint32_t nowMs);
void StopQuantum();
// Forwarded by spawn.cpp's one game-pack runtime release hook (no extra kernel slot).
void ReleaseQuantumOwner(const void* owner);
}
