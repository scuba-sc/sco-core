#pragma once
// GPL-3.0-only. Portable game-pack policy and host-only synchronous seam.
#include <sc_prefabs.h>
#include <cstring>
namespace sco::game::services {
// Callbacks nonthrowing and nonreentrant. reserve checks shared and owner quotas
// without side effects; spawn must be failure-atomic or retain its own cleanup
// on fault; remember is allocation-free after reserve and registers with the
// existing game.entities ledger (including never-streamed entities).
struct PrefabProvider {
    bool (*on_thread)() = nullptr;
    bool (*live)(const sco_plugin*) = nullptr;
    bool (*ready)() = nullptr;
    sco_result (*reserve)(const sco_plugin*) = nullptr;
    sco_result (*zone)(uint64_t) = nullptr;
    sco_result (*spawn)(const char*, uint64_t, const double*, const double*, uint64_t*) = nullptr;
    void (*remember)(sco_plugin*, uint64_t) = nullptr;
};
bool ValidPrefabPath(const char* path);
sco_result SpawnOwnedPrefab(const PrefabProvider&, sco_plugin*, const char*, uint64_t,
    const double*, const double*, uint64_t*);
// Owner-approved R1 default, not Windows/SEH/gameplay verification. No env bypass.
inline constexpr bool kPrefabConfirmed = true;

// What spawn.cpp reports when the spawn.prefabs rows did not resolve on the running build.
inline constexpr const char* kPrefabUnavailable = "prefab spawning isn't available";

// game.entities.prefabs is ready only when the spawner and its release hook are up AND the
// spawn.prefabs rows resolved: without them the first placement could only fail. faulted is the
// process-lifetime latch for a real engine fault.
inline constexpr bool PrefabServiceReady(bool started, bool canSpawn, bool releaseHooked,
                                         bool rowsResolved, bool faulted) {
    return started && canSpawn && releaseHooked && kPrefabConfirmed && rowsResolved && !faulted;
}

// kPrefabUnavailable means "rows missing" only while the spawner is otherwise up; when it is
// stopped, the missing rows are not the cause and the capability reason must not say so.
inline constexpr bool PrefabUnavailableMeansRowsMissing(bool started, bool canSpawn, bool releaseHooked) {
    return started && canSpawn && releaseHooked;
}

// How a refusal from the native spawner maps onto the service result. Only a real engine fault
// latches the capability off for the rest of the run; absent rows and unknown content do not.
enum class PrefabOutcome { NotFound, Unavailable, Fault };
inline PrefabOutcome ClassifyPrefabError(const char* error) {
    if (!error) return PrefabOutcome::Fault;
    if (!std::strcmp(error, "unknown prefab content path") || !std::strcmp(error, "unknown entity class"))
        return PrefabOutcome::NotFound;
    if (!std::strcmp(error, kPrefabUnavailable)) return PrefabOutcome::Unavailable;
    return PrefabOutcome::Fault;
}
}
