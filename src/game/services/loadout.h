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
Result StartLoadout(const LoadoutBackend* backend = nullptr);
void StopLoadout();
}
struct sco_loadout_backend;
namespace sco::game::services { sco_result SetLoadoutBackend(const sco_loadout_backend* backend); }

// Host provider socket for game.loadout (2.4.0, K-29): the host shell binds a custom simulation/equip
// backend. Host-source seam only, like LoadoutBackend above; not a plugin-facing table, so it is
// not declared in the public header sc_loadout.h. Game thread, once, before services::Start (which
// otherwise starts the service without a backend): SCO_BAD_ARG for a null/short/incomplete struct,
// SCO_UNAVAILABLE when the service is already started (nothing is replaced), else StartLoadout's result.
// Admission is host::FreshAdmissionAllowed(): refused until the host composition has enabled it, and
// re-checked live before every equip. Installing the struct is the host's attestation of its equip.
extern "C" {
struct sco_loadout_backend {
    uint32_t size;
    uint32_t weapons; /* 1 = backend supports weapon slots 8-21 */
    sc_loadout_status (*equip)(uint32_t slot, const char* item_class);
};
sco_result sco_game_set_loadout_backend(const sco_loadout_backend* backend);
}
