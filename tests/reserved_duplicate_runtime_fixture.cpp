// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Negative control: actual isolated sco_runtime sources in an unmanaged DSO.
#include "sco/runtime.h"
namespace { int owner, table; }
extern "C" __attribute__((visibility("default"))) int duplicate_runtime_provide() {
    return static_cast<int>(sco::ProvideService(&owner, nullptr, "fixture.service", 1, &table));
}
extern "C" __attribute__((visibility("default"))) int duplicate_runtime_query() {
    const void* result = nullptr;
    return static_cast<int>(sco::QueryService("fixture.service", 1, &result));
}
