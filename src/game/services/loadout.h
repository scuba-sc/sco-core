// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/runtime.h"
#include "sc_loadout.h"
namespace sco::game::services {
// Host/game-pack seam only: copied at Start, never exposed to plugins. The game
// adapter must attest typed per-slot methods and supply a live fail-closed offline
// admission check (unknown/online/EAC-present -> false). No backend is enabled by
// current production composition. Callbacks live until Stop; game thread only.
// `weapons` attests that equip also handles the 1.1 weapon/multitool slots 8-21
// (SC_LOADOUT_SLOT_PRIMARY..SIDEARM_TOOL_BATTERY); without it those slots are
// UNSUPPORTED. Even with it they stay UNAVAILABLE until capability
// game.loadout.weapons is on (default off, cleared on a provider fault).
struct LoadoutBackend {
    bool verified = false;
    bool (*offline)() = nullptr;
    sc_loadout_status (*equip)(uint32_t slot, const char* item_class) = nullptr;
    bool weapons = false;
};
// Async native boundary. Session must identify one uninterrupted local player.
// begin starts one slot once; terminal may be an immediate refusal. Otherwise
// token is nonzero and poll observes the same operation; never starts it again.
// discard releases pending native resources, never removes confirmed equipment.
struct LoadoutAsyncBackend {
    bool verified = false;
    bool (*admit)(uint64_t* session) = nullptr;
    bool (*begin)(uint32_t slot, const char* name, uint64_t* token,
                  sc_loadout_status* terminal) = nullptr;
    bool (*poll)(uint64_t token, bool* complete, sc_loadout_status* result) = nullptr;
    void (*discard)(uint64_t token) = nullptr;
};
Result StartLoadout(const LoadoutBackend* backend = nullptr, const LoadoutAsyncBackend* async = nullptr);
void TickLoadout(uint32_t now);
void ReleaseLoadoutOwner(const void* owner);
const LoadoutAsyncBackend* ProductionLoadoutBackend(); // Windows game pack
void StopLoadout();
}
