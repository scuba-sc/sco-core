#pragma once
// GPL-3.0-only. Portable game-pack policy and host-only synchronous seam.
#include <sc_prefabs.h>
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
// Owner-run Windows streamed spawn/unload cleanup acceptance required. No env bypass.
inline constexpr bool kPrefabConfirmed = false;
}
