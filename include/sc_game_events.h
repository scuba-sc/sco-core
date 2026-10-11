/* sc_game_events.h: the game.* events the Star Citizen game pack puts on the kernel event bus.
 * This header ships with the sco SDK; there is no service table: you subscribe by name with
 * sco_api's subscribe (or sco.subscribe in Lua), and the bus hands your callback a pointer to one
 * of the structs below as its data argument.
 *
 *   static void on_died(const char* event, const void* data, void* ctx) {
 *       const sc_game_player_died* d = (const sc_game_player_died*)data;
 *       if (d->size >= sizeof(*d)) { ... d->entity_id ... }
 *   }
 *   api->subscribe(self, SC_GAME_EVENT_PLAYER_DIED, on_died, NULL);
 *
 * Rules (docs/design/game-services.md, decision 8):
 *  - Low volume only: these are rare events. game.actors.died is the widest, one per death of any
 *    actor; it is a notification, not a stream, and entity streaming is not here (it is a
 *    type-filtered watch on game.entities).
 *  - Callbacks run on the game thread, from the host's tick, a moment (at most a frame or two)
 *    after the game did it. The game pack's hooks only record the occurrence; they never run
 *    plugin code.
 *  - Every payload starts with uint32_t size: the size the game pack built. A later minor appends
 *    fields; check size before reading one past the end of what you were built against. The
 *    pointer is valid only during the callback: copy what you keep.
 *  - Ids are the game's 64-bit entity and zone ids: session handles, never keys to store.
 *  - Each event has a capability (sco_api has()); an event whose capability isn't ready never
 *    fires, so subscribing is harmless: "game.events.player_spawned", "game.events.player_died",
 *    "game.events.actor_died", "game.events.zone_changed", "game.events.vehicle_seat".
 *    Both views use one death detour. game.actors.died fires for every captured death and
 *    game.player.died only for yours; a view whose capability is off never fires.
 */
#ifndef SC_GAME_EVENTS_H
#define SC_GAME_EVENTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_GAME_EVENT_PLAYER_SPAWNED  "game.player.spawned"
#define SC_GAME_EVENT_PLAYER_DIED     "game.player.died"
#define SC_GAME_EVENT_ACTOR_DIED      "game.actors.died"
#define SC_GAME_EVENT_ZONE_CHANGED    "game.zone.changed"
#define SC_GAME_EVENT_VEHICLE_BOARDED "game.vehicle.boarded"
#define SC_GAME_EVENT_VEHICLE_EXITED  "game.vehicle.exited"

/* game.player.spawned: your player has spawned (after a load or a respawn). */
typedef struct sc_game_player_spawned {
    uint32_t size;
    uint32_t _pad;
    uint64_t entity_id; /* your entity (the same as game.actors' local_player entity id) */
    uint64_t zone_id;   /* the zone it is in, 0 if it couldn't be read yet */
} sc_game_player_spawned;

/* game.player.died: your player died. */
typedef struct sc_game_player_died {
    uint32_t size;
    uint32_t _pad;
    uint64_t entity_id; /* your entity */
    uint64_t killer_id; /* always 0 in 1.0: the game's death record doesn't pin who did it */
} sc_game_player_died;

/* game.actors.died: an actor died, whichever actor it was: your player, an NPC, another player.
 * This is the game's own death path seen once, by the one hook that owns it. It carries no
 * judgement about whose death it was: game.player.died is the local-player view of the same
 * occurrence, published alongside this one on the tick. Only the dying entity's id is stable
 * enough to name here, so it is captured by the hook before the game's own code runs; when the
 * id can't be read (the game build's rows aren't OK, or the actor's handle is unreadable) it is
 * 0 and the event still fires, so a subscriber counts occurrences without trusting the id. */
typedef struct sc_game_actor_died {
    uint32_t size;
    uint32_t _pad;
    uint64_t entity_id; /* the dying entity, or 0 when the id couldn't be read */
    uint64_t killer_id; /* always 0 in 1.0: the game's death record doesn't pin who did it */
} sc_game_actor_died;

/* game.zone.changed: your player's zone id changed from one zone to another. Not published while
 * you are not spawned (game.player.spawned carries the zone you spawn in). */
typedef struct sc_game_zone_changed {
    uint32_t size;
    uint32_t _pad;
    uint64_t old_zone_id;
    uint64_t new_zone_id;
} sc_game_zone_changed;

/* game.vehicle.boarded and game.vehicle.exited share this payload. RESERVED: the game pack
 * doesn't publish them yet. Its hooks are found, but nothing in the game's code says which actor
 * a seat transition belongs to or whether the link is a vehicle seat (it may be a chair or a bed),
 * so game.events.vehicle_seat stays off until an in-game run confirms it
 * (docs/design/game-world-spikes.md, B8). */
typedef struct sc_game_vehicle_seat {
    uint32_t size;
    int32_t  seat_index;        /* game.vehicles' seat index, -1 when unknown */
    uint64_t vehicle_entity_id; /* the vehicle (the ship's entity id) */
} sc_game_vehicle_seat;

#ifdef __cplusplus
}
#endif

#endif
