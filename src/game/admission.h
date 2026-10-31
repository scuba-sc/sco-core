// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include "sco/game/host.h"
namespace sco::game::host::detail {
// Host composition only. Inspector is immutable once installed and retained for
// the process; it must never point to plugin code or unloadable shell storage.
void SetFreshInspector(Admission (*inspect)());
void SetFreshShutdownOwner(std::shared_ptr<OwnedShutdown> owner);
void SetFreshAdmissionEnabled(bool enabled);
// Game-thread host teardown only; never permits ordinary fresh service access.
void SetCvarCleanupEnabled(bool enabled);
bool CvarCleanupAllowed() noexcept;
}
