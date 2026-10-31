// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "../src/reserved_legacy_gate.h"
extern "C" const void* reserved_foreign_gate(unsigned which) {
    switch (which) {
    case 1: return &sco::detail::g_authorityMutex;
    case 2: return &sco::detail::g_legacyEntries;
    case 3: return &sco::detail::g_legacyDirty;
    case 4: return &sco::detail::g_reserved;
    default: return nullptr;
    }
}
