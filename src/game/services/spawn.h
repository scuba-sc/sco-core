#pragma once
// spawn.entities inside the game pack: started and stopped by services::Start / Stop (spatial.cpp),
// after teleport.spatial, whose zone tree its mover converts through. Game thread.
#include "sco/runtime.h"
#include <cstdint>

namespace sco::game::services {

// Private game-pack composition. Spawn's existing cleanup dispatcher, world control
// and owned bookkeeping share ONE neutral hook. Each participant can start/stop on
// its own (including portable fixtures); only the last removal releases that slot.
// The bounded participant list is not a change to sco::kMaxReleaseHooks.
namespace release_detail {
inline ReleaseHook hooks[4] = {};
inline size_t count = 0;
inline void Dispatch(const void* owner) {
    // Match runtime release semantics: callbacks may change registrations, so take
    // a snapshot before calling anything, without retaining plugin state.
    ReleaseHook pending[4] = {};
    const size_t n = count;
    for (size_t i = 0; i < n; ++i) pending[i] = hooks[i];
    for (size_t i = 0; i < n; ++i) pending[i](owner);
}
} // namespace release_detail

inline Result AddGameReleaseHook(ReleaseHook hook) {
    if (!OnGameThread()) return Result::WrongThread;
    if (!hook) return Result::BadArg;
    using namespace release_detail;
    for (size_t i = 0; i < count; ++i) if (hooks[i] == hook) return Result::Ok;
    if (count == 4) return Result::TooMany;
    if (!count) {
        const auto r = AddReleaseHook(Dispatch);
        if (r != Result::Ok) return r; // no participant admitted without cleanup
    }
    hooks[count++] = hook;
    return Result::Ok;
}

inline Result RemoveGameReleaseHook(ReleaseHook hook) {
    if (!OnGameThread()) return Result::WrongThread;
    using namespace release_detail;
    for (size_t i = 0; i < count; ++i) {
        if (hooks[i] != hook) continue;
        for (size_t j = i + 1; j < count; ++j) hooks[j - 1] = hooks[j];
        hooks[--count] = nullptr;
        if (!count) RemoveReleaseHook(Dispatch);
        return Result::Ok;
    }
    return Result::NotFound;
}

// Publishes spawn.entities 1.2 and joins the game release dispatcher. BadArg when
// the name is taken; nothing is left published then.
Result StartSpawn();
void   StopSpawn();

// For game.actors (actors.cpp): spawns entityClass at pos in zone zoneId's local frame through
// the spawner spawn.entities uses, facing rot (x y z w, normalised; nullptr = the zone's axes).
// nullptr with id set on success, else the reason ("unknown entity class", "the spawner isn't
// available on this game build", ...). Game thread.
const char* SpawnEntityInZone(const char* entityClass, uint64_t zoneId, const double pos[3], uint64_t& id,
                              const double rot[4] = nullptr);

// For game.entities (entities.cpp): moves and turns entity id to pos and rot (x y z w, normalised)
// given in zone zoneId's frame, through the same mover as spawn.entities' set_entity_transform,
// but without its ownership rules (the caller applies its own). Game thread.
enum class MoveResult : uint8_t { Ok, NotStreamedIn, NoZone, ZoneConversion, Refused };
MoveResult MoveEntity(uint64_t id, uint64_t zoneId, const double pos[3], const double rot[4]);

// True while spawn's cleanup participant is registered with the shared dispatcher:
// it also releases game.actors' NPCs, so game.actors spawns nothing without it.
bool SpawnReleaseHooked();
// Internal only, validated canonical path/pose, same batch/mover/removal as entities.
// A nonzero id on error must remain tracked until cleanup (release-batch fault).
const char* SpawnPrefabEntityInZone(const char*, uint64_t, const double*, const double*, uint64_t&);

// For game.vehicles' ownership rules. Game thread. SpawnedBy: owner spawned id through spawn_as
// and is still loaded (false without the release hook, as the mover). IsPlayerVehicle: the product
// registered id (RegisterPlayerVehicle).
bool SpawnedBy(const void* owner, uint64_t id);
bool IsPlayerVehicle(uint64_t id);

}  // namespace sco::game::services
