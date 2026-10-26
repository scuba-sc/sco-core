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
// Epoch is a current copied spatial observation, while instance is a distinct
// opaque token that changes on replacement, reparenting or authority reset.
struct RelocationPositionTarget {
    uint64_t epoch = 0, instance = 0, system = 0;
    bool streamed = false;
};
struct RelocationPositionBackend {
    bool verified = false;
    bool (*target)(uint64_t zoneId, RelocationPositionTarget* out) = nullptr;
    // Revalidate the admitted instance before applying; never call a full-pose transport.
    bool (*apply)(const sc_relocation_position_request* request, uint64_t instance) = nullptr;
    // Fresh position only, and only in the admitted target frame/instance.
    bool (*observe)(uint64_t zoneId, uint64_t instance, double pos[3]) = nullptr;
};
// Game thread. A null backend publishes the service with game.relocation OFF.
Result StartRelocation(const RelocationBackend* backend = nullptr);
Result StartRelocationV2(const RelocationBackend* backend, const RelocationPositionBackend* position);
void StopRelocation();
// Game thread, once per game tick: drives bounded work and refreshes the capability.
void TickRelocation();
void ReleaseRelocationOwner(const void* owner);
// The Windows engine adapter's backend (relocation_engine.cpp); verified only when its rows resolved.
const RelocationBackend* EngineRelocationBackend();
}
struct sco_relocation_backend;
struct sco_relocation_backend_v2;
namespace sco::game::services {
sco_result SetRelocationBackend(const sco_relocation_backend* backend);
sco_result SetRelocationBackendV2(const sco_relocation_backend_v2* backend);
}

// Host provider socket for game.relocation (2.4.0, K-29): the host shell binds a custom simulation/
// relocation backend. Host-source seam only, like RelocationBackend above; not a plugin-facing table,
// so it is not declared in the public header sc_relocation.h. The callbacks are RelocationBackend's:
// player, target, apply (its move) and pose are all required, now is optional (null = steady clock).
// Game thread, once, before services::Start (which otherwise starts the service with the engine
// backend): SCO_BAD_ARG for a null/short/incomplete struct, SCO_UNAVAILABLE when the service is already
// started (nothing is replaced), else StartRelocation's result. Admission is host::FreshAdmissionAllowed():
// refused until the host composition has enabled it, and re-checked live on every request and tick.
// Installing the struct is the host's attestation that its callbacks are verified.
extern "C" {
struct sco_relocation_backend {
    uint32_t size;
    uint32_t _pad;
    bool (*player)(sco::game::services::RelocationPlayer* out);
    bool (*target)(uint64_t zone_id, sco::game::services::RelocationTarget* out);
    bool (*apply)(const sc_relocation_request* request);
    bool (*pose)(uint64_t zone_id, double pos[3], double rot[4]);
    uint64_t (*now)();
};
sco_result sco_game_set_relocation_backend(const sco_relocation_backend* backend);

// Host-source 1.1 socket. The old socket/installer above is unchanged. Position
// callbacks are all required; FullPose target/apply/pose are either all present
// or all absent. player is required, now optional. Installation attests these
// callbacks, including independent target-instance authority, as verified. Same
// fresh host admission, game-thread/one-shot-before-start and lifetime rules.
// The production engine adapter supplies none of these new position callbacks.
struct sco_relocation_backend_v2 {
    uint32_t size;
    uint32_t _pad;
    bool (*player)(sco::game::services::RelocationPlayer* out);
    bool (*target)(uint64_t zone_id, sco::game::services::RelocationTarget* out);
    bool (*apply)(const sc_relocation_request* request);
    bool (*pose)(uint64_t zone_id, double pos[3], double rot[4]);
    uint64_t (*now)();
    bool (*position_target)(uint64_t zone_id, sco::game::services::RelocationPositionTarget* out);
    bool (*apply_position)(const sc_relocation_position_request* request, uint64_t instance);
    bool (*observe_position)(uint64_t zone_id, uint64_t instance, double pos[3]);
};
sco_result sco_game_set_relocation_backend_v2(const sco_relocation_backend_v2* backend);
}
