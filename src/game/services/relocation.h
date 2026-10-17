#pragma once
// SPDX-License-Identifier: GPL-3.0-only
#include "sco/runtime.h"
#include "sc_relocation.h"
namespace sco::game::services {
// What the backend reports about the live local player. Every field is 0 when unknown.
struct RelocationPlayer {
    uint64_t id = 0;        // the game's id for the player's actor
    uint64_t instance = 0;  // an opaque token for the live player entity; a different value is a different session
    uint64_t system = 0;    // id of the star system the player is in (the "SolarSystem" zone); 0 = unknown
};
// What the backend reports about a destination zone.
struct RelocationTarget {
    uint64_t system = 0;    // id of the star system the zone is in; 0 = unknown
    bool streamed = false;  // the zone is loaded right now
};
// Host/game-pack seam only: copied at Start, never exposed to plugins. The engine adapter
// (relocation_engine.cpp) supplies the transport and the system classifier; the host supplies the
// offline proof. Callbacks run on the game thread under the provider's fault guard, live until Stop,
// and are only called while `offline` reports a positive, fresh host admission. `verified` is the
// adapter's attestation that its rows resolved on this game build. Any missing callback or an
// unverified backend keeps game.relocation OFF. A callback that throws or faults latches the provider
// off until the process restarts.
struct RelocationBackend {
    bool verified = false;
    bool (*offline)() = nullptr;                       // fresh host-certified offline admission; unknown/online -> false
    bool (*player)(RelocationPlayer* out) = nullptr;   // false when no live local player
    bool (*target)(uint64_t zoneId, RelocationTarget* out) = nullptr;
    bool (*move)(const sc_relocation_request* request) = nullptr;  // initiates the movement; false = not initiated
    // The player's fresh observed pose in the frame of zone zoneId (position, x y z w rotation);
    // false unless the player is currently in that zone.
    bool (*pose)(uint64_t zoneId, double pos[3], double rot[4]) = nullptr;
    uint64_t (*now)() = nullptr;                       // monotonic milliseconds; null = steady clock
};
// Game thread. A null backend publishes the service with game.relocation OFF.
Result StartRelocation(const RelocationBackend* backend = nullptr);
void StopRelocation();
// Game thread, once per game tick: drives bounded work and refreshes the capability.
void TickRelocation();
void ReleaseRelocationOwner(const void* owner);
// The Windows engine adapter's backend (relocation_engine.cpp); verified only when its rows resolved.
const RelocationBackend* EngineRelocationBackend();
}

// Host provider socket for game.relocation (2.4.0, K-29): the host shell binds a custom simulation/
// relocation backend. Host-source seam only, like RelocationBackend above; not a plugin-facing table,
// so it is not declared in the public header sc_relocation.h.
extern "C" {
struct sco_relocation_backend {
    uint32_t size;
    uint32_t _pad;
    int (*evidence)(uint64_t target_zone, const sc_relocation_request* pending, void* out_evidence);
    int (*apply)(const sc_relocation_request* request);
};
sco_result sco_game_set_relocation_backend(const sco_relocation_backend* backend);
}
