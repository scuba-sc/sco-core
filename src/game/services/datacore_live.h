#pragma once
// SPDX-License-Identifier: GPL-3.0-only
#include "sc_datacore_live.h"

namespace sco::game::services::datacore_live {
// Copied host-only backend. Callbacks remain live through Stop. No native pool
// adapter is supplied; the host attests layout, fresh admission and pool lifetime.
struct Backend {
    bool verified = false;
    bool (*offline)() = nullptr;
    bool (*ready)() = nullptr;
    sco_result (*stats)(uint32_t, sc_datacore_pool_stats*) = nullptr;
    sco_result (*find)(const uint8_t[16], sc_datacore_live_record_info*) = nullptr;
    sco_result (*read)(uint32_t, uint64_t, void*, size_t, size_t*) = nullptr;
    sco_result (*write)(uint32_t, uint64_t, const void*, size_t, size_t*) = nullptr;
};
sco_result Start(const Backend* backend = nullptr);
void Stop();
void SetWritesEnabled(bool enabled);
}
