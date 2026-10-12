/* SPDX-License-Identifier: GPL-3.0-only
 * Independent native consumers: public SDK headers only, no host imports.
 * Synthetic inspector / owned-NPC / actor-death workflows, not game certification. */
#include "sco_api.h"
#include "sc_actors.h"
#include "sc_entities.h"
#include "sc_game_events.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef CONSUMER_ID
#define CONSUMER_ID "conformance"
#endif
/* Check the complete pointer extent before reading even a null tail member. */
#define MEMBER(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
static const sco_api* api;
static sco_plugin* self;
static uint64_t npc;
static unsigned deaths;
static int npcOwned;

static sco_result Actors(const sc_actors_v1** out) {
    const void* table = NULL;
    sco_result r = api->query_service(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &table);
    *out = r == SCO_OK ? (const sc_actors_v1*)table : NULL;
    return r;
}
static sco_result Entities(const sc_entities_v1** out) {
    const void* table = NULL;
    sco_result r = api->query_service(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, &table);
    *out = r == SCO_OK ? (const sc_entities_v1*)table : NULL;
    return r;
}
static sco_result Inspect(const sco_arg* args, uint32_t n, void* ctx, char* reply, uint32_t size) {
    const sc_actors_v1* a;
    const sc_entities_v1* e;
    uint64_t actor = 0, entity = 0, zone = 0;
    float cur = 0, max = 0;
    double pos[3] = {0}, rot[4] = {0};
    sco_result r;
    (void)args; (void)n; (void)ctx;
    if (!api->has("game.actors.health") || !api->has("game.entities.transform")) return SCO_UNAVAILABLE;
    if ((r = Actors(&a)) != SCO_OK) return r;
    if ((r = Entities(&e)) != SCO_OK) return r;
    if (!MEMBER(a, sc_actors_v1, local_player) || !MEMBER(a, sc_actors_v1, health) ||
        !MEMBER(e, sc_entities_v1, alive) || !MEMBER(e, sc_entities_v1, get_transform)) return SCO_UNAVAILABLE;
    if ((r = a->local_player(&actor, &entity)) != SCO_OK) return r;
    if (!e->alive(entity)) return SCO_NOT_FOUND;
    if ((r = a->health(entity, &cur, &max)) != SCO_OK) return r;
    if ((r = e->get_transform(entity, pos, rot, &zone)) != SCO_OK) return r;
    /* A zero maximum is unknown, never a fabricated 0%/100% reading. */
    snprintf(reply, size, "entity=%llu health=%.0f max=%s zone=%llu x=%.0f",
             (unsigned long long)entity, (double)cur, max == 0 ? "unknown" : "known",
             (unsigned long long)zone, pos[0]);
    return SCO_OK;
}
static sco_result Spawn(const sco_arg* args, uint32_t n, void* ctx, char* reply, uint32_t size) {
    const sc_actors_v1* a;
    const double pos[3] = {1, 2, 3};
    sco_result r;
    (void)args; (void)n; (void)ctx; (void)reply; (void)size;
    if (!api->has("game.actors.spawn_npc")) return SCO_UNAVAILABLE;
    if ((r = Actors(&a)) != SCO_OK) return r;
    if (!MEMBER(a, sc_actors_v1, spawn_npc)) return SCO_UNAVAILABLE;
    if (npcOwned) return SCO_TOO_MANY;
    r = a->spawn_npc(self, "SyntheticNpc", 9, pos, &npc);
    npcOwned = r == SCO_OK;
    return r;
}
static sco_result Observe(const sco_arg* args, uint32_t n, void* ctx, char* reply, uint32_t size) {
    const sc_entities_v1* e;
    sco_result r;
    (void)args; (void)n; (void)ctx;
    if ((r = Entities(&e)) != SCO_OK) return r;
    if (!MEMBER(e, sc_entities_v1, alive)) return SCO_UNAVAILABLE;
    snprintf(reply, size, "%s", e->alive(npc) ? "streamed" : "pending-or-gone");
    return SCO_OK;
}
static sco_result Remove(const sco_arg* args, uint32_t n, void* ctx, char* reply, uint32_t size) {
    const sc_actors_v1* a;
    sco_result r;
    (void)args; (void)n; (void)ctx; (void)reply; (void)size;
    if ((r = Actors(&a)) != SCO_OK) return r;
    if (!MEMBER(a, sc_actors_v1, despawn)) return SCO_UNAVAILABLE;
    /* Peer has not spawned: try the first consumer's synthetic entity, not a wildcard clear. */
    r = a->despawn(self, npc ? npc : 700);
    if (r == SCO_OK) npcOwned = 0;
    return r;
}
static sco_result Violate(const sco_arg* args, uint32_t n, void* ctx, char* reply, uint32_t size) {
    static const uint32_t table = sizeof(uint32_t);
    (void)args; (void)n; (void)ctx; (void)reply; (void)size;
    return api->provide_service(self, SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &table);
}
static void Task(void* ctx) {
    (void)ctx;
    api->log(self, SCO_LOG_INFO, "conformance task ran");
}
static sco_result Queue(const sco_arg* args, uint32_t n, void* ctx, char* reply, uint32_t size) {
    (void)args; (void)n; (void)ctx; (void)reply; (void)size;
    return api->run_on_game_thread(self, Task, NULL);
}
static void Death(const char* event, const void* data, void* ctx) {
    sc_game_actor_died copied;
    (void)event; (void)ctx;
    /* Only read the size prefix until the complete payload is known to exist. */
    if (!data || *(const uint32_t*)data < sizeof(copied)) return;
    memcpy(&copied, data, sizeof(copied));
    ++deaths; /* zero entity id remains a valid occurrence; retain no borrowed pointer */
}
static sco_result Stats(const void* in, uint32_t in_size, void* out, uint32_t* size, void* ctx) {
    (void)in; (void)in_size; (void)ctx;
    if (!size) return SCO_BAD_ARG;
    if (!out || *size < sizeof(deaths)) { *size = sizeof(deaths); return SCO_TOO_MANY; }
    memcpy(out, &deaths, sizeof(deaths));
    *size = sizeof(deaths);
    return SCO_OK;
}
static sco_result Register(const char* name, sco_command_fn fn) {
    sco_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.size = sizeof(cmd); cmd.name = name; cmd.title = name; cmd.fn = fn;
    return api->register_command(self, &cmd);
}
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) {
    static const sco_plugin_info info = {sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
                                         CONSUMER_ID, "1.0.0", "tests"};
    return &info;
}
SCO_EXPORT sco_result sco_plugin_load(const sco_api* host, sco_plugin* owner) {
    sco_result r;
    if (!MEMBER(host, sco_api, query_service) || !MEMBER(host, sco_api, register_raw) ||
        !MEMBER(host, sco_api, has) || !MEMBER(host, sco_api, register_command) ||
        !MEMBER(host, sco_api, subscribe) || !MEMBER(host, sco_api, unsubscribe) ||
        !MEMBER(host, sco_api, run_on_game_thread) || !MEMBER(host, sco_api, log) ||
        !MEMBER(host, sco_api, provide_service)) return SCO_UNAVAILABLE;
    api = host; self = owner; npc = 0; deaths = 0; npcOwned = 0;
    if ((r = Register(CONSUMER_ID ".inspect", Inspect)) != SCO_OK) return r;
    if ((r = Register(CONSUMER_ID ".spawn", Spawn)) != SCO_OK) return r;
    if ((r = Register(CONSUMER_ID ".observe", Observe)) != SCO_OK) return r;
    if ((r = Register(CONSUMER_ID ".remove", Remove)) != SCO_OK) return r;
    if ((r = Register(CONSUMER_ID ".queue", Queue)) != SCO_OK) return r;
    if ((r = Register(CONSUMER_ID ".violate", Violate)) != SCO_OK) return r;
    if ((r = api->register_raw(self, CONSUMER_ID ".stats", NULL, Stats, NULL)) != SCO_OK) return r;
    return api->subscribe(self, SC_GAME_EVENT_ACTOR_DIED, Death, NULL);
}
SCO_EXPORT void sco_plugin_unload(void) {
    api->unsubscribe(self, SC_GAME_EVENT_ACTOR_DIED, Death);
    if (npcOwned) {
        const sc_actors_v1* a;
        if (Actors(&a) == SCO_OK && MEMBER(a, sc_actors_v1, despawn)) a->despawn(self, npc);
    }
}
