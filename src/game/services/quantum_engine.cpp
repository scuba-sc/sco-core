// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// The default production game.quantum backend on Windows, ported from sc-offline v0.7.7
// src/quantum.cpp and src/cvars.cpp under the maintainer's 2026-10-15 amended rule
// (docs/quantum.md). Every layout constant below is a v0.7.7 value; every function and cvar
// storage comes from a signature row. The bookkeeping (admission, first-prior save, restore,
// ownership) lives in quantum_owned.cpp and is tested synthetically; this file only reads, writes
// and calls under SEH. Nothing here has run in the game.
#include "quantum_owned.h"
#include "../admission.h"
#include "../../api/internal.h"
#include "sco/game/host.h"
#include "sco/game/reads.h"
#include "sco/hook.h"
#include "sco/signatures.h"
#include <cmath>
#include <windows.h>

namespace sco::game::services {

namespace {

constexpr uint64_t kPtrMask = 0xFFFFFFFFFFFFull;
constexpr size_t kUsableToDrive = 0x1C0, kDriveParams = 0x18, kParamDisableCharge = 0xC0;
constexpr size_t kDriveNewFlag = 0xB60, kDriveReason = 0xC3C, kDriveState = 0xC48, kDriveInput = 0xBB0;
constexpr size_t kDrivePowered = 0xC68, kDriveTravelBlocked = 0xC69, kDriveBoostBlocked = 0xC6A, kDriveCharge = 0xCA8;

// The four cvars the amended rule lets the backend own. Each row resolves the int storage that
// world_sigs.cpp's ResolveCVar finds through the cvar's int registration (slot 0x40), which is
// the type check; logging, target-lock thresholds and p_enable_physical_quantum_travel
// (member storage, no global) are not owned.
constexpr const char* kCVarRows[kOwnedCVars] = {
    "cvars.quantum_travel_allowed", "cvars.quantum_boost_allowed",
    "cvars.ignore_blocked_boost", "cvars.ignore_blocked_travel",
};
constexpr const char* kHookRows[] = { "quantum.start_use", "quantum.charge", "quantum.effect_update" };

struct QuantumInput { int32_t action, mode; float value; };
using StartUseFn = void(__fastcall*)(uintptr_t usable, uintptr_t user);
using ChargeFn = uint8_t(__fastcall*)(uintptr_t drive, float alignment, float dt, uint8_t blocked);
using EffectUpdateFn = uintptr_t(__fastcall*)(uintptr_t handler, uint64_t state, double a3, uint64_t a4,
                                              uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8);
using DriveInputFn = void(__fastcall*)(uintptr_t drive, const QuantumInput* input);

StartUseFn g_startUse = nullptr;
ChargeFn g_charge = nullptr;
EffectUpdateFn g_effectUpdate = nullptr;
DriveInputFn g_driveInput = nullptr;

template <typename T> T Rd(uintptr_t p) { return *reinterpret_cast<const T*>(p); }

int32_t* CVarStorage(uint32_t i) {
    return i < kOwnedCVars && SigReady(kCVarRows[i]) ? reinterpret_cast<int32_t*>(Sig(kCVarRows[i])) : nullptr;
}

bool Admitted(void*) {
    __try { return sco::game::host::FreshAdmissionAllowed(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool LocalUser(void*, uintptr_t user) {
    if (!user) return false;
    __try {
        uintptr_t actor = 0, entity = 0;
        return reads::LocalPlayer(actor, entity) && (user == actor || user == entity);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

QuantumCVarType CVarType(void*, uint32_t i) { return CVarStorage(i) ? QuantumCVarType::Int32 : QuantumCVarType::Unknown; }

bool CVarRead(void*, uint32_t i, int32_t* out) {
    const int32_t* s = CVarStorage(i);
    if (!s) return false;
    __try { *out = *s; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool CVarWrite(void*, uint32_t i, int32_t value) {
    int32_t* s = CVarStorage(i);
    if (!s || (value != 0 && value != 1)) return false;
    __try { *s = value; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool DriveIdentity(void*, uintptr_t drive, uintptr_t* id) {
    if (!drive) return false;
    __try {
        if (!Rd<uint8_t>(drive + kDriveNewFlag)) return false;
        *id = Rd<uintptr_t>(drive);
        return *id != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool DriveSample(void*, uintptr_t drive, QuantumDriveSample* out) {
    __try {
        out->state = Rd<int32_t>(drive + kDriveState);
        out->reason = Rd<int32_t>(drive + kDriveReason);
        out->powered = Rd<uint8_t>(drive + kDrivePowered);
        out->travelBlocked = Rd<uint8_t>(drive + kDriveTravelBlocked);
        out->boostBlocked = Rd<uint8_t>(drive + kDriveBoostBlocked);
        out->input = Rd<uint8_t>(drive + kDriveInput);
        out->charge = Rd<float>(drive + kDriveCharge);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ParamCell(void*, uintptr_t drive, uintptr_t* cell) {
    __try {
        const uintptr_t record = Rd<uintptr_t>(drive + kDriveParams);
        if (!record) return false;
        *cell = record + kParamDisableCharge;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ChargeCell(void*, uintptr_t drive, uintptr_t* cell) { *cell = drive + kDriveCharge; return true; }

bool CellRead(void*, uintptr_t cell, float* out) {
    __try { *out = Rd<float>(cell); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool CellWrite(void*, uintptr_t cell, float value) {
    __try { *reinterpret_cast<float*>(cell) = value; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SendInput(void*, uintptr_t drive, int32_t action, int32_t mode, float value) {
    const bool press = mode == kInputPress && value == 1.0f, release = mode == kInputRelease && value == 0.0f;
    if (!g_driveInput || action != kInputAction || (!press && !release)) return false;
    const QuantumInput input = { action, mode, value };
    __try { g_driveInput(drive, &input); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint32_t NowMs(void*) { return GetTickCount(); }

const QuantumEngineOps kOps = { nullptr, Admitted, LocalUser, CVarType, CVarRead, CVarWrite, DriveIdentity,
                                DriveSample, ParamCell, ChargeCell, CellRead, CellWrite, SendInput, NowMs };
OwnedQuantum g_owner(kOps);

bool EffectHandlerLinked(uintptr_t handler) {
    __try { return handler && (Rd<uint64_t>(handler) & kPtrMask); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void __fastcall StartUseHook(uintptr_t usable, uintptr_t user) {
    g_startUse(usable, user);
    if (OnGameThread() && usable >= kUsableToDrive) g_owner.OnStartUse(usable - kUsableToDrive, user);
}

uint8_t __fastcall ChargeHook(uintptr_t drive, float alignment, float dt, uint8_t blocked) {
    const uint8_t result = g_charge(drive, alignment, dt, blocked);
    return OnGameThread() && g_owner.KeepFull(drive) ? static_cast<uint8_t>(3) : result;
}

uintptr_t __fastcall EffectUpdateHook(uintptr_t handler, uint64_t state, double a3, uint64_t a4,
                                      uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8) {
    if (!OnGameThread() || !g_owner.ControlActive() || EffectHandlerLinked(handler))
        return g_effectUpdate(handler, state, a3, a4, a5, a6, a7, a8);
    return 0;
}

QuantumHook g_hooks[3];

}

Result ConfigureDefaultProductionQuantum() {
    if (!OnGameThread() || !Admitted(nullptr)) return Result::Unavailable;
    for (const char* row : kHookRows)
        if (!SigReady(row)) return Result::Unavailable;
    for (uint32_t i = 0; i < kOwnedCVars; ++i)
        if (!SigReady(kCVarRows[i])) return Result::Unavailable;
    if (!SigReady("quantum.drive_input")) return Result::Unavailable;
    g_driveInput = reinterpret_cast<DriveInputFn>(Sig("quantum.drive_input"));
    g_hooks[0] = { Sig("quantum.start_use"), 5, reinterpret_cast<void*>(&StartUseHook), reinterpret_cast<void**>(&g_startUse) };
    g_hooks[1] = { Sig("quantum.charge"), 5, reinterpret_cast<void*>(&ChargeHook), reinterpret_cast<void**>(&g_charge) };
    g_hooks[2] = { Sig("quantum.effect_update"), 8, reinterpret_cast<void*>(&EffectUpdateHook), reinterpret_cast<void**>(&g_effectUpdate) };
    return ConfigureProductionQuantum(MakeOwnedQuantumBackend(g_owner, g_hooks, 3));
}

}
