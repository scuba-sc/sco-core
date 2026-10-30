#pragma once
// The Star Citizen game pack's services (docs/game-services.md), published under the owner
// "game" with host::ProvideGameService. sco::app::Start calls Start when the product sets
// Platform::gameServices and the game pack's services are built (SCO_GAME_SC, Windows: the
// sco_game_services library, which defines SCO_GAME_SERVICES); Stop calls Stop after UnloadAll.
//
// Today: teleport.spatial 1.0 (sc_spatial.h), spawn.entities 1.2 (sc_spawn.h), game.actors 1.0
// (sc_actors.h) and game.vehicles 1.0 (sc_vehicles.h). teleport.spatial answers 0 until the
// teleport.* rows are resolved (the product publishes its resolved snapshot before Start) and you've spawned;
// spawn.entities answers "the spawner isn't available on this game build" (and 0) unless the
// spawn.helpers rows (sco/game/actors.h) were OK at Start too; game.actors sets the capabilities
// game.actors.local_player, .spawn_npc and .despawn, and game.vehicles game.vehicles.seats, .seat
// and .flight_ready, and game.creative game.creative.god_mode, .fly, .ammo and .ship_ammo, from
// their rows at Start; each answers SCO_UNAVAILABLE for a function whose capability isn't ready.
// game.creative 1.0 (sc_creative.h) is the local player's god mode, fly and infinite ammo; the
// toggles a plugin switched on go off when it unloads (spawn.cpp's release hook).
#include "sco/runtime.h"
#include <cstdint>

namespace sco::game::services {

// Game thread. Publishes the services and subscribes their tick. BadArg when a name is taken
// (a product still provides teleport.spatial or spawn.entities itself); nothing is left published
// then.
Result Start();
void   Stop();
bool   Started();

// The switch for game.relocation 1.0 (sc_relocation.h), settable by the product from a player or host
// setting. Any thread. Default on. Off stops new requests, fails a pending one and turns the
// capability game.relocation off; on never bypasses anything: the capability still needs the host's
// certified offline admission, a verified engine backend, a live player and a known star system.
void SetRelocationEnabled(bool enabled);
bool RelocationEnabled();

// The switch for game.actor_stats_write 1.0 (sc_actor_stats_write.h): a product sets it from a
// player or host setting. Any thread. Default OFF; the environment variable
// SCO_ACTOR_STATS_WRITE=1, read once when game.actors starts, turns it on for a maintainer's
// in-game test without a product change. On never bypasses anything: the capability still needs
// its rows, game.actor_stats.read, and every write still needs fresh offline admission.
void SetActorStatWritesEnabled(bool enabled);
bool ActorStatWritesEnabled();

// For the game pack's own code (spawn.entities' mover): the same frames as teleport.spatial.
// Game thread. 0 / false when the entity or a zone isn't streamed in.
uint64_t ZoneOfEntity(uint64_t entityId);
bool     PoseToZone(uint64_t from, uint64_t to, const double pos[3], const double rotXyzw[4],
                    double outPos[3], double outRotXyzw[4]);

// The player's own vehicle, retrieved or delivered by ATC: once the product registers its id, any
// plugin may move it with spawn.entities' set_entity_transform. Game thread. Registering an id
// twice or unregistering one that isn't registered changes nothing; ids stay registered across
// Stop and Start (the product unregisters them).
void RegisterPlayerVehicle(uint64_t entityId);
void UnregisterPlayerVehicle(uint64_t entityId);

}  // namespace sco::game::services
