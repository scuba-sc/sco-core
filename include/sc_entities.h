/* sc_entities.h: the game.entities service: ask about any entity (is it streamed in, its class,
 * where it is), spawn and despawn entities of your own anywhere, move what you may move, and watch
 * entities stream in and out. This header ships with the sco SDK; the Star Citizen game pack
 * publishes the service under the owner "game" (sco::host::ProvideGameService, when the product
 * sets Platform::gameServices; docs/game-services.md). The sc_ prefix marks a game or product
 * service: sco_ is reserved for host services. To use it:
 *
 *   const sc_entities_v1* ent = NULL;
 *   if (api->size > offsetof(sco_api, query_service) &&
 *       api->query_service(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, (const void**)&ent) == SCO_OK) { ... }
 *
 * The version is the query_service argument, never a field: query_service answers SCO_UNAVAILABLE
 * when the published table is older than the version asked for. A later 1.x minor only appends
 * functions; check size before calling one: ent->size > offsetof(sc_entities_v1, <function>).
 * 1.1 appends keep (the table is 96 bytes; a 1.0 caller asked for 1.0 and never reads past
 * last_error). Check ent->size > offsetof(sc_entities_v1, keep) before calling keep.
 *
 * Rules (docs/design/game-services.md):
 *  - Game thread only (a command, a tick or a run_on_game_thread task). From another thread every
 *    function except last_error answers SCO_WRONG_THREAD (alive: 0) without touching the game.
 *  - Functions that change the game, or register something, take your plugin's handle (self, as
 *    sco_plugin_load received it) first. A handle that is unknown or already released is
 *    SCO_BAD_ARG.
 *  - Ownership: an entity you spawn here is yours. Only you may despawn it, and the host despawns
 *    every entity you spawned here, and still own, when your plugin unloads or crashes. Your
 *    watches are withdrawn then too. Nothing you registered outlives your plugin.
 *  - Moving: set_transform moves entities you spawned (here, with spawn.entities' spawn_as, or as
 *    an NPC with game.actors) and the player's own vehicles, which sc-offline registers once
 *    they are retrieved or delivered (any plugin may move those). Nothing else.
 *  - Keeping (1.1): keep gives up your ownership of an entity you spawned here, so that it stays
 *    in the world when your plugin unloads or crashes (a build mode's props must outlive a
 *    reload of the build plugin). A kept entity is nobody's: the host never despawns it, and
 *    despawn and set_transform by anyone are refused like any entity the caller doesn't own.
 *    No SDK removal authority survives keep. Build undo/clear must instead opt into
 *    game.owned retain_build (sc_owned.h): ownership stays until unload, not across reload.
 *    keep is refused for those retained placements. A legacy kept entity stops counting against
 *    SC_ENTITIES_MAX_OWNED.
 *  - Ids are the game's 64-bit entity ids: session handles that name something only while it is
 *    streamed in, never keys to store. Store the class name instead.
 *  - Nothing is allocated across the boundary; text and ids come back through caller buffers.
 *  - Every failure leaves a reason for last_error.
 *  - Capabilities (sco_api has()): "game.entities.transform" (get_transform, set_transform),
 *    "game.entities.spawn" (spawn, despawn), "game.entities.class_of", "game.entities.query_radius"
 *    and "game.entities.watch". Each is ready when its signature rows are OK on this game build;
 *    query_radius and watch also stay off until an in-game check has confirmed what the rows
 *    can't (docs/game-services.md). A function whose capability isn't ready answers
 *    SCO_UNAVAILABLE.
 *
 * Positions are teleport.spatial's (sc_spatial.h): metres as doubles in a zone's local frame;
 * rotations are quaternions x, y, z, w. Zone ids are volatile streaming handles (player_pose,
 * zone_of_entity, get_transform): never keep one past the moment you got it. */
#ifndef SC_ENTITIES_H
#define SC_ENTITIES_H

#include "sco_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_ENTITIES_NAME        "game.entities"
#define SC_ENTITIES_VERSION_1_0 0x00010000u
#define SC_ENTITIES_VERSION_1_1 0x00010001u /* + keep */
#define SC_ENTITIES_VERSION_1_2 0x00010002u /* + copied lookup observations */
#define SC_ENTITIES_MAX_CLASSES 64u
#define SC_ENTITY_COMPONENT_ACTOR 1u
#define SC_ENTITY_COMPONENT_INVENTORY 2u

/* Host-managed class identity, not a truncated native class key or pointer.
 * All records are caller-owned historical copies; epoch expires on registry
 * change, tick or session stop. Names are exact, NUL-terminated, <=95 bytes.
 * Unknown tail bytes are untouched; known prefix zeroed on failure. */
typedef struct sc_entity_class_record {
    uint32_t size;
    uint32_t _pad;
    uint64_t epoch;
    uint64_t class_id;
    char name[96];
} sc_entity_class_record;
typedef struct sc_entity_class_page {
    uint32_t size;
    uint32_t count; /* capacity in, copied count out, <=64; zero counts only */
    uint32_t more;
    uint32_t _pad;
    uint64_t epoch;
    sc_entity_class_record records[SC_ENTITIES_MAX_CLASSES];
} sc_entity_class_page;
/* Presence observation only, not component data or a native address. Requery
 * before use; generation changes on stream-out/reload. Only the two explicit
 * kinds above are admitted. No arbitrary type name, field or memory reader. */
typedef struct sc_entity_component_record {
    uint32_t size;
    uint32_t kind;
    uint64_t entity_id;
    uint64_t generation;
} sc_entity_component_record;

/* Entities spawned through game.entities and not yet despawned, all plugins together. */
#define SC_ENTITIES_MAX_OWNED 1024u
/* Watches registered, all plugins together. */
#define SC_ENTITIES_MAX_WATCHES 64u
/* Longest watch type, NUL excluded. */
#define SC_ENTITIES_MAX_TYPE_LEN 63u

/* watch's `what` (a mask) and the callback's `what` (the one that happened). */
#define SC_ENTITY_STREAMED_IN  1u
#define SC_ENTITY_STREAMED_OUT 2u

/* Called on the game thread, once per matching entity: ctx as given to watch, which event, the
 * entity's id and its class name (valid only during the call). For STREAMED_OUT the id is the
 * entity's last moment: the entity is gone by the time you are called, so alive(id) answers 0.
 * Calling unwatch from inside the callback is fine, and so is your plugin being unloaded from
 * inside it: no further event reaches you. */
typedef void (*sc_entity_watch_fn)(void* ctx, uint32_t what, uint64_t entity_id, const char* class_name);

typedef struct sc_entities_v1 {
    uint32_t size; /* sizeof(sc_entities_v1) as the game pack built it */
    uint32_t _pad;
    /* 1 when the entity is streamed in, else 0 (also when the capability isn't ready or off the
     * game thread). */
    int (*alive)(uint64_t entity_id);
    /* The entity's class name (the name spawn takes), NUL-terminated, with the size handshake
     * (*inout_size: capacity in; bytes written, or needed with SCO_TOO_MANY, out). SCO_NOT_FOUND:
     * the entity isn't streamed in. SCO_UNAVAILABLE: no "game.entities.class_of". SCO_FAILED: the
     * game faulted or the name isn't a plausible class name. Read-only: no self; a failure's
     * reason is last_error(NULL, ...). */
    sco_result (*class_of)(uint64_t entity_id, char* out, uint32_t* inout_size);
    /* The entity's position (3 doubles) and rotation (x, y, z, w) in its zone's local frame, and
     * that zone's id. Any out pointer may be NULL to skip it, not all of them. Outputs are
     * zeroed on failure. SCO_NOT_FOUND: the entity isn't streamed in, or isn't in a zone.
     * SCO_UNAVAILABLE: no "game.entities.transform". Read-only: last_error(NULL, ...). */
    sco_result (*get_transform)(uint64_t entity_id, double out_pos[3], double out_rot_xyzw[4], uint64_t* out_zone_id);
    /* Moves and turns an entity you may move (see Moving above): pos and rot are in zone zone_id's
     * local frame (any streamed-in zone; the host converts to the entity's own zone). rot is
     * normalised. SCO_BAD_ARG: a null or released self, id 0, a null pos or rot, a position that
     * isn't finite or a zero rotation. SCO_NOT_FOUND: not streamed in yet (a fresh spawn takes a
     * few seconds: wait for alive), or a zone that isn't. SCO_UNAVAILABLE: no
     * "game.entities.transform". SCO_FAILED: not yours to move, or the game refused. */
    sco_result (*set_transform)(sco_plugin* self, uint64_t entity_id, uint64_t zone_id, const double pos[3],
                                const double rot_xyzw[4]);
    /* Spawns an entity of entity_class (a class name, as in sc-offline's npcs.txt or spawn.entities)
     * at pos, facing rot, in zone zone_id's local frame: anywhere, not only near the player. *out_id
     * = the new entity id, 0 on failure. The id is final at once; the entity streams in a few
     * seconds later. It is yours (see Ownership above). SCO_BAD_ARG: a null or released self, a
     * null class, pos, rot or out_id, zone 0, or a position that isn't finite or a zero rotation.
     * SCO_NOT_FOUND: the zone isn't streamed in, or the class doesn't exist on this game build.
     * SCO_TOO_MANY: SC_ENTITIES_MAX_OWNED are alive. SCO_UNAVAILABLE: no "game.entities.spawn", or
     * the host couldn't remove your entities when you unload. SCO_FAILED: the game refused or
     * faulted. */
    sco_result (*spawn)(sco_plugin* self, const char* entity_class, uint64_t zone_id, const double pos[3],
                        const double rot_xyzw[4], uint64_t* out_id);
    /* Removes an entity you spawned here. SCO_OK starts the removal: the entity leaves the world
     * within a few seconds (one that hasn't streamed in yet goes once it has), and the id is no
     * longer yours. SCO_NOT_FOUND: not an entity you spawned here, or despawned already. */
    sco_result (*despawn)(sco_plugin* self, uint64_t entity_id);
    /* Calls fn(ctx, what, id, class) on the game thread for every entity of `type` that streams
     * in or out (`what`: SC_ENTITY_STREAMED_IN, SC_ENTITY_STREAMED_OUT or both ORed), until
     * unwatch or until your plugin unloads. *out_watch_id = the watch's id (never 0).
     * type: an entity class name ("AEGS_Avenger_Titan"), matched exactly and case-sensitively, or
     * a class prefix ending in one '*' ("AEGS_*"). The host matches the type before calling you,
     * so you pay only for what you asked about. Refused with SCO_BAD_ARG, and a reason: NULL,
     * empty, a lone "*", a '*' that isn't last ("A*B", "**"), more than
     * SC_ENTITIES_MAX_TYPE_LEN characters, anything but printable ASCII or '?', a `what` with no
     * known bit, a null self, fn or out_watch_id. SCO_TOO_MANY: SC_ENTITIES_MAX_WATCHES exist.
     * SCO_UNAVAILABLE: no "game.entities.watch" on this build, or while the hooks are
     * unconfirmed; see docs/game-services.md. Events reach fn a tick after they happen, in order;
     * if a plugin can't keep up, the oldest are dropped. */
    sco_result (*watch)(sco_plugin* self, uint32_t what, const char* type, sc_entity_watch_fn fn, void* ctx,
                        uint64_t* out_watch_id);
    /* Ends a watch you registered; fn isn't called again once this returns. SCO_NOT_FOUND: not
     * your watch, or ended already. */
    sco_result (*unwatch)(sco_plugin* self, uint64_t watch_id);
    /* Ids of streamed-in entities within radius metres of pos in zone zone_id's frame, whose
     * class matches class_filter (a class name or a "PREFIX*"; NULL or "" = any class). Up to max ids are written to out_ids (max 0 counts only);
     * *out_count = how many were written and *out_more = how many matched but didn't fit.
     * SCO_BAD_ARG: a null pos, out_count or out_more, a null out_ids with max > 0, zone 0, a
     * radius that isn't finite and positive, a filter that isn't a class name or prefix.
     * SCO_NOT_FOUND: the zone isn't streamed in. SCO_FAILED: the walk faulted or saw more than
     * 65536 entities. SCO_UNAVAILABLE: no
     * "game.entities.query_radius" on this game build, or while its in-game check is pending
     * (docs/game-services.md). Read-only: last_error(NULL, ...). */
    sco_result (*query_radius)(uint64_t zone_id, const double pos[3], double radius, const char* class_filter,
                               uint64_t* out_ids, uint32_t max, uint32_t* out_count, uint32_t* out_more);
    /* The reason for self's last failed call ("" if none), NUL-terminated, with the size
     * handshake (*inout_size: capacity in; bytes written, or needed with SCO_TOO_MANY, out).
     * self NULL: the last failure of a call made without a plugin handle (alive's neighbours:
     * class_of, get_transform, query_radius). Any thread. */
    sco_result (*last_error)(sco_plugin* self, char* out, uint32_t* inout_size);
    /* 1.1. Releases your ownership of an entity you spawned here (see Keeping above): the host no
     * longer despawns it when your plugin unloads or crashes, and set_transform and despawn by
     * you are refused from now on, as for any entity you don't own. The entity itself is
     * untouched. SCO_OK, once. SCO_BAD_ARG: a null or released self, id 0. SCO_NOT_FOUND: not an
     * entity you spawned here and still own (kept or despawned already, or spawned another way),
     * with a reason for last_error. SCO_FAILED: game.owned retained placement (use remove_build
     * instead). SCO_UNAVAILABLE: no "game.entities.spawn". Call it right
     * after spawn succeeds: the id is final at once, the entity needn't have streamed in. */
    sco_result (*keep)(sco_plugin* self, uint64_t entity_id);
    /* 1.2. Copied full registry bounded to64 entries, NOT observed class_of
     * names. epoch=0 starts; nonzero requires exact current epoch. Overflow
     * reports more; offset indexes sorted class_id order. No partial writes on
     * malformed provider snapshot; stale epoch NOT_FOUND, absent UNAVAILABLE. */
    sco_result (*list_classes)(uint64_t epoch, uint32_t offset, sc_entity_class_page* out);
    /* Exact case-sensitive class name; unknown NOT_FOUND. */
    sco_result (*find_class)(const char* name, sc_entity_class_record* out);
    /* Debug-grade instance name lookup, ASCII case-insensitive. Ambiguous
     * names FAILED rather than choosing a traversal-dependent result. */
    sco_result (*find_by_name)(const char* name, uint64_t* out_entity_id);
    /* Validated typed presence for a streamed entity. No component pointer or
     * persistent token. NOT_FOUND absent entity/component, BAD_ARG bad kind. */
    sco_result (*component)(uint64_t entity_id, uint32_t kind, sc_entity_component_record* out);
} sc_entities_v1;

#ifdef __cplusplus
}
#endif

#endif
