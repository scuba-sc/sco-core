// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include "quantum.h"
#include <cstdint>
namespace sco::game::services {
// Platform-neutral bookkeeping behind the default production quantum backend. It owns the
// maintainer's 2026-10-15 amended rule (docs/quantum.md, "Amended rule"): the backend may write
// four int quantum cvars, one shared drive parameter cell and the local drive's charge, ONLY
// while a hold is active, after positive fresh admission before each write, with the first prior
// value saved before the first write and restored on every release path. All engine access goes
// through QuantumEngineOps, so the Windows adapter (quantum_engine.cpp) and the synthetic tests
// drive the same logic.
enum class QuantumCVarType : uint32_t { Unknown = 0, Int32 = 1 };
constexpr uint32_t kOwnedCVars = 4;
constexpr int32_t kOwnedCVarValue = 1;
constexpr float kDisableChargeOff = 1e9f;
constexpr int32_t kInputAction = 582, kInputPress = 1, kInputRelease = 2;
constexpr uint32_t kChargeSettleMs = 3000;

struct QuantumDriveSample {
    int32_t state = 0, reason = 0;
    uint8_t powered = 0, travelBlocked = 0, boostBlocked = 0, input = 0;
    float charge = 0;
};

// Every callback is guarded by the adapter (no exception or fault may escape) and reports false
// when it could not complete. A null callback is a build error in the adapter, not a runtime case.
struct QuantumEngineOps {
    void* ctx = nullptr;
    bool (*admitted)(void*) = nullptr;                       // positive fresh offline admission
    bool (*localUser)(void*, uintptr_t user) = nullptr;      // exact match with the local player
    QuantumCVarType (*cvarType)(void*, uint32_t index) = nullptr;
    bool (*cvarRead)(void*, uint32_t index, int32_t* out) = nullptr;
    bool (*cvarWrite)(void*, uint32_t index, int32_t value) = nullptr;
    bool (*driveIdentity)(void*, uintptr_t drive, uintptr_t* id) = nullptr;  // false: not the new drive
    bool (*driveSample)(void*, uintptr_t drive, QuantumDriveSample* out) = nullptr;
    bool (*paramCell)(void*, uintptr_t drive, uintptr_t* cell) = nullptr;
    bool (*chargeCell)(void*, uintptr_t drive, uintptr_t* cell) = nullptr;
    bool (*cellRead)(void*, uintptr_t cell, float* out) = nullptr;
    bool (*cellWrite)(void*, uintptr_t cell, float value) = nullptr;
    bool (*sendInput)(void*, uintptr_t drive, int32_t action, int32_t mode, float value) = nullptr;
    uint32_t (*nowMs)(void*) = nullptr;
};

class OwnedQuantum {
public:
    explicit OwnedQuantum(const QuantumEngineOps& ops) : ops_(ops) {}
    bool Observe(QuantumObservation* out);
    bool Apply(bool enabled, bool held);
    // StartUse hook tail (the original already ran). Acts only for an exact local-user match.
    void OnStartUse(uintptr_t drive, uintptr_t user);
    // Charge hook tail: true when the charge was kept full and the hook should report 3.
    bool KeepFull(uintptr_t drive);
    bool ControlActive();
    bool Faulted() const { return fault_; }
    uintptr_t Drive() const { return drive_; }
private:
    enum class Step { Ok, Lost, Failed };
    struct CVarRecord { bool saved = false, written = false; int32_t prior = 0, wrote = 0; };
    struct CellRecord { bool written = false; uintptr_t drive = 0, cell = 0; float prior = 0, wrote = 0; };
    bool Admit() { return ops_.admitted(ops_.ctx); }
    bool DriveAlive();
    void DropDrive();
    Step WriteCVar(uint32_t i);
    Step EnsureParam();
    Step SyncInput(bool held);
    bool RestoreCVar(uint32_t i);
    bool RestoreCell(CellRecord& rec, bool requireDrive);
    bool ReleaseInput();
    bool Release();
    QuantumEngineOps ops_;
    CVarRecord cvars_[kOwnedCVars];
    CellRecord param_, charge_;
    uintptr_t drive_ = 0, identity_ = 0;
    bool active_ = false, pressed_ = false, fault_ = false, chargeAnchored_ = false;
    uint32_t chargeSince_ = 0;
};

// Backend whose ctx is `owner`; both must outlive the provider (process lifetime in production).
QuantumBackend MakeOwnedQuantumBackend(OwnedQuantum& owner, const QuantumHook* hooks, uint32_t hookCount);
}
