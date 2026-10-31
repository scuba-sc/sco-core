// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include "sco/runtime.h"
#include <sc_mining.h>
namespace sco::game::services {
// Host-only seam, NOT a plugin service: all callbacks/storage are immutable,
// process-retained, thread-safe game-pack/host code. Never plugin callbacks.
// read returns false only on an unsafe read/fault; ordinary ineligibility is data.
struct MiningCell { int32_t builderType = -1; uint8_t spawnGate = 1; int32_t harvestLod = -1; };
struct MiningBackend {
    bool ready = false;
    bool cleanupReady = false;
    bool (*admitted)() = nullptr;
    bool (*install)() = nullptr;
    bool (*read)(uintptr_t builder, uintptr_t cell, MiningCell* out) = nullptr;
};
Result StartMining(bool cleanupReady = false); // production adapter remains unavailable until native verification
Result StartMining(const MiningBackend& backend);
// Includes an attempted installer: failure does not establish rollback.
bool MiningHookRetained() noexcept;
void StopMining();
void ReleaseMiningOwner(const void* owner); // existing spawn owner-release dispatcher
// Native worker route; no plugin callbacks/pointers. Counts every native cell.
void MiningReadFault(); // native SEH refusal latches restart
uint32_t MiningCellFlags(uintptr_t builder, uintptr_t cell, uint32_t lod, uint32_t flags);
#ifdef _WIN32
MiningBackend NativeMiningBackend(bool cleanupReady);
#endif
}
