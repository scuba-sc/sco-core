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
//
// `respawnCompleted` attests that the adapter has verified the native respawn-loadout-completed
// occurrence (F-INTAKE-004 names it; its dispatch site and calling convention are unverified, T-3,
// not run in game) and will call RecordRespawnLoadoutCompleted() from it. It needs `verified` and
// `offline`. Only then is capability game.events.loadout_respawn_completed on and
// game.loadout.respawn_completed published; default off, cleared on a provider fault or Stop.
struct LoadoutBackend {
    bool verified = false;
    bool (*offline)() = nullptr;
    sc_loadout_status (*equip)(uint32_t slot, const char* item_class) = nullptr;
    bool weapons = false;
    bool respawnCompleted = false;
};
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
const LoadoutAsyncBackend* ProductionLoadoutBackend();
// Failed retains uncertain provider resources and requires host backing state to stay alive.
Result StopLoadout();
// Host producer seam for the verified adapter's native hook: records one occurrence and never runs
// plugin code. Safe from any thread (one generation-tagged atomic state). Returns
// false, recording nothing, unless the respawnCompleted backend is started and the capability is
// on. The event is dispatched later on the game thread from the host's "tick" (live offline
// admission is re-checked then; refused occurrences are dropped, not replayed). Occurrences within
// one tick coalesce into one event.
bool RecordRespawnLoadoutCompleted();
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
