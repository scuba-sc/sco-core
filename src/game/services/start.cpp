// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/game/services.h"

namespace sco::game::services {
// Public host-source entry: do not inspect or mutate native state until a
// durable exclusion proof has a separately reviewed implementation.
Result Start() { return Result::Unavailable; }
}
