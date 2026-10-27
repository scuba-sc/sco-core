// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_api.h"
#include <cstdint>

namespace sco::app {

// Host-source seam only; neither this descriptor nor the reserved owner is a plugin ABI.
inline constexpr uint32_t SCO_HOST_MENU_CLIENT_VERSION = 1;
struct HostMenuContext {
    const sco_api* api = nullptr;
    sco_plugin* self = nullptr;
    uint64_t generation = 0;
};
struct HostMenuClient {
    void* context = nullptr;
    sco_result (*load)(const HostMenuContext&, void*) = nullptr;
    void (*frame)(const HostMenuContext&, void*, void*) = nullptr;
    void (*stop)(const HostMenuContext&, void*) = nullptr;
};

// Game-thread only. A copied context remains permanently inactive after its instance ends.
bool HostMenuActive(const HostMenuContext& context);
bool HostMenuFrame(void* frameContext);
// Latch a platform frame failure that occurred outside the guarded client thunk.
void HostMenuAbort();
bool IsHostMenuPanel(const char* id);
} // namespace sco::app
