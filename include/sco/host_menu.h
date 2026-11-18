// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_api.h"
#include "sco/widgets_view.h"
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

// Game thread. Valid only for the reserved host-menu identity of an active HostMenuClient
// (HostMenuActive(context) is true). Returns false for any other caller or after stop, and
// leaves both outputs null. Both outputs are static: valid for the life of the process, never
// freed or reused. After the client's stop callback returns, every call through them answers
// SCO_UNAVAILABLE (menu_thread_leave excepted).
bool HostWidgetsView(const HostMenuContext& context, const sco_widgets_view_v1** table,
                     sco_view_client** client);

// Control thread, for the same reserved identity, and only after the menu thread has been joined:
// clears a menu-thread registration that the thread did not clear itself (it died on a fault).
// Returns false for any other caller or when nothing was registered. The caller must not call it
// while the menu thread may still run; a drain that timed out leaves the registration to that
// thread's own menu_thread_leave.
bool HostWidgetsViewReleaseMenuThread(const HostMenuContext& context);
} // namespace sco::app
