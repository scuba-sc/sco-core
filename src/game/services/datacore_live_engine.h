// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once

#include "datacore_live.h"
#include "../datacore_live_lifecycle.h"

#include <cstdint>

namespace sco::game::services::datacore_live {

using LifecycleGate = sco::game::datacore_live::LifecycleGate;

struct EngineRows {
    uintptr_t ownerSlot = 0;
    uintptr_t ownerVtable = 0;
    uintptr_t reset = 0;
    uintptr_t deletingDestructor = 0;
    LifecycleGate* gate = nullptr;
    bool (*admitted)() = nullptr;
};
struct LifecycleTargets { void* reset=nullptr; void* destructor=nullptr; void* deleting=nullptr; };
// Caller enforces quiescence before this call and until all hooks are installed.
bool PrepareEngine(const EngineRows&,const LifecycleTargets&,bool preEngineQuiesced) noexcept;
bool PrepareProductionEngine(bool preEngineQuiesced) noexcept;
bool EnginePrepared() noexcept;

// Bind only after startup has established pre-engine quiescence and installed
// all lifecycle hooks. Reset clears the binding; it does not enable writes.
bool ConfigureEngine(const EngineRows& rows) noexcept;
void ClearEngine() noexcept;
Backend EngineBackend() noexcept;

} // namespace sco::game::services::datacore_live
