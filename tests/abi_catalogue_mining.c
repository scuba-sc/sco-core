/* SPDX-License-Identifier: GPL-3.0-only
 * SPDX-FileCopyrightText: 2026 the sco-core contributors
 * Catalogue C11 layout evidence only; no provider or engine execution. */
#include "sc_mining.h"
#include <stddef.h>
_Static_assert(sizeof(sc_mining_v1) == 24, "mining table layout");
_Static_assert(offsetof(sc_mining_v1, request) + sizeof(((sc_mining_v1*)0)->request) == 16,
               "mining request extent");
_Static_assert(offsetof(sc_mining_v1, state) + sizeof(((sc_mining_v1*)0)->state) == 24,
               "mining state extent");
_Static_assert(sizeof(sc_mining_state) == 40, "mining output layout");
_Static_assert(offsetof(sc_mining_state, cells) == 16, "mining counters offset");
