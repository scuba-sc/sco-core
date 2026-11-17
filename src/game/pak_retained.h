// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
// Private. Game host process-retained composition only; not a public or plugin header.
#include "sco/game/pak.h"

namespace sco::game::pak::retained {
// The real Enable for a host that retains the hook until process exit (game::host
// Composition::ProcessRetained). The shell's install-quiescence attestation (barrier, called with
// context) is checked here, immediately before the patch: null, false or a throw is Unavailable and
// nothing is installed. Scope::DataCoreLoad only. After Ok the hook is latched: Disable() is a
// refused no-op, so the detour, the mounts and vfs.pak stay in place. The public Enable stays gated
// for everyone else.
Result Enable(const Options& options, bool (*barrier)(void*), void* context);
Result Enable(const Options& options, const Targets& targets, bool (*barrier)(void*), void* context);
}
