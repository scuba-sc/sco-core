// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/runtime.h"
#include "sc_loadout.h"
namespace sco::game::services {
// Host/game-pack seam only: copied at Start, never exposed to plugins. The game
// adapter must attest typed per-slot methods and supply a live fail-closed offline
// admission check (unknown/online/EAC-present -> false). No backend is enabled by
// current production composition. Callbacks live until Stop; game thread only.
struct LoadoutBackend {
    bool verified = false;
    bool (*offline)() = nullptr;
    sc_loadout_status (*equip)(uint32_t slot, const char* item_class) = nullptr;
};
Result StartLoadout(const LoadoutBackend* backend = nullptr);
void StopLoadout();
}
