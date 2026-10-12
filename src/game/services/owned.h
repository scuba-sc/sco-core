#pragma once
#include "sco/runtime.h"
#include <cstdint>
namespace sco::game::services {
// Composes existing ownership/despawn/seat primitives; started by game.entities.
Result StartOwned();
void StopOwned();
bool BuildRetainedBy(const void* owner, uint64_t id);
void DropBuildRetention(const void* owner, uint64_t id);
// Strict entities ownership, not MayMove's player/spawn_as/NPC union.
bool EntitiesOwnedBy(const void* owner, uint64_t id);
}
