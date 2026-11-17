// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
// Private. The process-retained host composition (Options::composition == ProcessRetained), a state
// machine separate from the strict one in host.cpp. Not a public or plugin header. host.cpp dispatches
// to it only when it was selected at Prepare; the two compositions exclude each other per process.
#include "sco/game/host.h"
#ifdef SCO_GAME_HOST_REPORT_TESTING
#include "sco/game/pak.h"
#include <cstdint>
#endif

namespace sco::game::host::retained {
// Strict Prepare attempts claim the process for the strict composition: false once the retained one
// was selected. The retained Prepare claims the other way. Neither claim is ever released.
bool ClaimStrict() noexcept;
bool Selected() noexcept;

Result Prepare(const Options& options);
Result Activate();
Result Start();
Result Tick(uint32_t nowMs);
Result Stop();
Result BeforeQuit();
Snapshot Status();

HostReport ActivateDetailed() noexcept;
HostReport StartDetailed() noexcept;
HostReport StopDetailed() noexcept;
HostReport RequestStop() noexcept;
HostReport StatusReport() noexcept;

#ifdef SCO_GAME_HOST_REPORT_TESTING
namespace testing {
using QuitFn = void (*)(void* system, void* info, int exitCode);
// Set only before creating test threads. No real hook is installed.
void SetQuitOriginalForTest(QuitFn) noexcept;
void InvokeQuitDetourForTest(void* system, void* info, int exitCode);
// Fake loader targets for the pak enable, so a synthetic image can exercise the hook step.
void SetPakTargetsForTest(const pak::Targets* targets) noexcept;
}
#endif
}
