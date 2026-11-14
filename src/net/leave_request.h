// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once

namespace sco::net::retained_closure {
// Private. Process-retained closure only: queue a leave for a running session without waiting for its
// network thread (net::Leave joins that thread). No-op when no session runs. Never throws.
void RequestLeave(const char* reason) noexcept;
}
