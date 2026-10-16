#pragma once
// game.actors inside the game pack: started and stopped by services::Start / Stop (spatial.cpp),
// after spawn.entities, whose spawner and release hook it uses. Game thread.
#include "sco/runtime.h"
#include <cstdint>

namespace sco::game::services {

// Publishes game.actors 1.1, sets its capabilities and subscribes its tick (the removal checks and
// the state probe).
// BadArg when the name is taken; nothing is left published then.
Result StartActors();
// Starts the removal of every NPC still owned (a plugin whose release failed), then withdraws.
void   StopActors();

// spawn.cpp's release hook: owner unloaded or crashed, so every NPC it spawned through
// game.actors leaves the world, and its last error is forgotten. Game thread.
void ReleaseActorsOwner(const void* owner);

// For game.entities (entities.cpp): the removal game.actors uses for its NPCs. EntityRemovalReady:
// game.actors is started and its removal rows (the game.actors.despawn capability) are OK.
// RemoveEntityLater(id) asks the game to remove entity id (once it has streamed in, if it hasn't)
// and checks it a moment later, like despawn. Game thread.
bool EntityRemovalReady();
void RemoveEntityLater(uint64_t id);

// For game.vehicles' ownership rule: owner spawned NPC id through game.actors' spawn_npc and still
// owns it (not despawned, owner not released). Game thread.
bool NpcOwnedBy(const void* owner, uint64_t id);

// The state probe (docs/design/game-world-spikes.md B4): logs what the game's two "not fully
// alive" checks answer for this actor (a CSCActor pointer, read under SEH), tagged with why it
// was asked ("died", "spawned"). Log only, and only when the actor.state_* rows are OK:
// game.actors.state stays off until an in-game run says which check is dead and which
// incapacitated. Game thread; safe with any pointer.
void ProbeActorState(uintptr_t actor, const char* why);

// For game.actor_stats (actor_stats.cpp): the CSCActorStatus of the actor whose entity id is id,
// found the way health finds it. ActorStatusReady: game.actors is started and game.actors.health
// is ready (the same rows); why gets the reason when it isn't. ActorStatusOf: Ok with *status set,
// NotFound (id not streamed in, not an actor), or Failed (no status object, or a fault); why gets
// a static reason. Game thread.
bool   ActorStatusReady(const char*& why);
Result ActorStatusOf(uint64_t id, uintptr_t& status, const char*& why);

// Started at the end of StartActors and stopped at the start of StopActors (actor_stats.cpp).
Result StartActorStats();
void   StopActorStats();

}  // namespace sco::game::services
