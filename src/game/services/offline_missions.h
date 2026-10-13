#pragma once
// Game-pack implementation seam, NOT a plugin/public host composition contract.
// Engine adapters must be independently owner-validated, offline admitted and fault-contained.
// The default has no backend: signatures alone never enable this service.
#include "sc_missions.h"
#include "sc_economy.h"
#include "sco/runtime.h"
namespace sco::game::offline {
struct Admission {
    uint64_t session = 0;
    bool offline = false, eac_absent = false, shutting_down = false;
};
struct Backend {
    void* ctx = nullptr;
    Admission (*admission)(void*) = nullptr;
    sco_result (*offer)(void*, uint32_t, sc_mission_offer*) = nullptr;
    sco_result (*status)(void*, uint64_t, sc_mission_status*) = nullptr;
    sco_result (*accept)(void*, const sc_mission_offer*, uint64_t*) = nullptr;
    sco_result (*objective)(void*, uint64_t, uint32_t, uint32_t) = nullptr;
    sco_result (*advance)(void*, uint64_t, uint32_t) = nullptr;
    sco_result (*abandon)(void*, uint64_t) = nullptr;
    sco_result (*ingest)(void*, const sc_mission_script*) = nullptr;
    sco_result (*balance)(void*, int64_t*) = nullptr;
    // Atomic compare/update; OK promises exact completed delta; any failure may be uncertain.
    sco_result (*apply)(void*, int64_t expected, int64_t delta, int64_t* after) = nullptr;
};
// Session-lifetime borrowed backend context. Start/Stop on game thread only.
// Start refuses replacement; Stop must follow consumer unload. Faults require process restart.
Result Start(const Backend& backend = {}, bool shared_release_hook = false);
Result Stop();
// Shared with spawn.cpp's release hook in a composed Windows host; portable fixtures own a hook.
void ReleaseOwner(const void* owner);
}
