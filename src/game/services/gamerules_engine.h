// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Native GameRules read seam (K-40). The parameter blob sits behind the game's
// IGameRulesParamsModule accessor chain. The only route this project has grounded applies state
// (gamerules.params_sync writes the blob), and no read-only accessor has been grounded, so
// production resolves nothing and refuses: the capability stays OFF and no native call is made.
//
// The seam exists so that refusal is explicit and testable rather than implied by an empty
// backend, and so a future grounded accessor plugs in without touching the public header, the
// frozen 1.0 state layout, or the 1.1 table. Not run in game.
#include "gamerules.h"
#include <cstdint>

namespace sco::game::services {

// Everything a host must supply before the read backend can be installed. Every field is
// required; a null one refuses with a reason instead of degrading into a partial backend.
struct GameRulesNativeRoute {
    // The IGameRulesParamsModule instance the host resolved for this build.
    void* params_module = nullptr;
    // Read-only accessor for the params blob: fills every named field of the frozen 1.0 state and
    // writes nothing. Not grounded on any build; the only known route applies state.
    bool (*read_only_accessor)(void* ctx, sc_gamerules_state* out) = nullptr;
    // Host-owned change counter: a nonzero generation that changes only when a value changed, so a
    // reader can reuse the copy it already has. The host owns the count; nothing here increments it.
    uint64_t (*generation)(void* ctx) = nullptr;
    void* ctx = nullptr;
    bool Complete() const { return params_module && read_only_accessor && generation; }
};

// Why the last install refused; empty after a successful one. Static storage, never caller-owned.
const char* NativeGameRulesRefusal();

// Installs the native read backend. Refuses, naming the first missing piece, unless the route is
// complete. A refusal leaves no backend installed and every capability exactly as it was.
Result InstallNativeGameRules(const GameRulesNativeRoute& route = {});

// The installed backend, or an empty one. read is null until an install succeeds.
GameRulesBackend NativeGameRulesBackend();

}  // namespace sco::game::services
