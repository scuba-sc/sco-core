// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Guard mechanics migrated from sc-offline src/builtins/mining_plugin.cpp,
// derived from junikka/sc-offline-mining src/mining.cpp (GPL-3.0).
// No legacy g_tp/SpawnerReady admission, product environment or plugin callback.
#include "mining.h"
#include "sco/game/mining.h"
#include "sco/game/host.h"
#include "sco/signatures.h"
#include "sco/hook.h"
#include <windows.h>
namespace sco::game::services {
namespace {
// The row/ABI has not been exercised in this host. Signature matches are NOT
// native execution or G1 shell acceptance. Keep production unavailable.
constexpr bool kNativeVerified = false;
using CellFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uint32_t, uint32_t, uint64_t, uint8_t);
CellFn original = nullptr;
bool FreshAdmission() {
    // SEH frame has no destructible C++ locals; inspector is host-only.
    __try { return sco::game::host::FreshAdmissionAllowed(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { MiningReadFault(); return false; }
}
bool Read(uintptr_t builder, uintptr_t cell, MiningCell* out) {
    __try {
        if (!builder || !cell || !out) return false;
        out->builderType = *reinterpret_cast<const int32_t*>(builder + mining::kCellBuilderType);
        out->spawnGate = *reinterpret_cast<const uint8_t*>(builder + mining::kCellSpawnGate);
        if (out->builderType != 0 || out->spawnGate != 0) return true;
        out->harvestLod = *reinterpret_cast<const int32_t*>(cell + mining::kCellHarvestLod);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } // provider latches read faults
}
void __fastcall CellHook(uintptr_t builder, uintptr_t cell, uintptr_t component,
                         uint32_t lod, uint32_t flags, uint64_t page, uint8_t option) {
    const uint32_t forwarded = MiningCellFlags(builder, cell, lod, flags);
    // Once, on its native caller/worker, with every argument preserved. Native
    // continuation exceptions propagate; never retry or call plugin code.
    original(builder, cell, component, lod, forwarded, page, option);
}
bool Install() {
    if (!kNativeVerified) return false;
    if (!FreshAdmission()) return false;
    if (original) return true;
    auto* target = Sig("mining.cell");
    if (!target || hook::IsHooked(target)) return false; // one address owner
    // First five bytes: mov [rsp+0x20], r9d, row-pinned whole non-relative instruction.
    return hook::InstallDetour(target, 5, reinterpret_cast<void*>(&CellHook),
                              reinterpret_cast<void**>(&original)) == hook::Error::None;
}
}
MiningBackend NativeMiningBackend(bool cleanupReady) {
    return {kNativeVerified && Sig("mining.cell") != nullptr, cleanupReady, FreshAdmission, Install, Read};
}
}
