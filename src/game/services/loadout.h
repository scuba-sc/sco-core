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
Result StartLoadout(const LoadoutBackend* backend = nullptr);
void StopLoadout();
// Host producer seam for the verified adapter's native hook: records one occurrence and never runs
// plugin code. Safe from any thread (one generation-tagged atomic state). Returns
// false, recording nothing, unless the respawnCompleted backend is started and the capability is
// on. The event is dispatched later on the game thread from the host's "tick" (live offline
// admission is re-checked then; refused occurrences are dropped, not replayed). Occurrences within
// one tick coalesce into one event.
bool RecordRespawnLoadoutCompleted();
}
