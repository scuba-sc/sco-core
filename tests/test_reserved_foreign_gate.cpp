// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/reserved_session.h"
#include "../src/reserved_legacy_gate.h"
#include <cstdio>
#include <cstdlib>
extern "C" const void* reserved_foreign_gate(unsigned);
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
int main() {
#if SCO_RESERVED_GATE_IMPORT_FIXTURE == 1
    CHECK(reserved_foreign_gate(1) == &sco::detail::g_authorityMutex);
#elif SCO_RESERVED_GATE_IMPORT_FIXTURE == 2
    CHECK(reserved_foreign_gate(2) == &sco::detail::g_legacyEntries);
#elif SCO_RESERVED_GATE_IMPORT_FIXTURE == 3
    CHECK(reserved_foreign_gate(3) == &sco::detail::g_legacyDirty);
#elif SCO_RESERVED_GATE_IMPORT_FIXTURE == 4
    CHECK(reserved_foreign_gate(4) == &sco::detail::g_reserved);
#else
#error expected one imported gate object
#endif
    CHECK(!sco::reserved::CreateExecutableAuthority());
}
