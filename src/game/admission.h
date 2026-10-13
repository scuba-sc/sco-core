// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include "sco/game/host.h"
namespace sco::game::host::detail {
// Host composition only. Inspector is immutable once installed and retained for
// the process; it must never point to plugin code or unloadable shell storage.
void SetFreshInspector(Admission (*inspect)());
void SetFreshAdmissionEnabled(bool enabled);
}
