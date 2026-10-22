/*
 * counter_service.h: the contract of the service "counter_service.counter", version 1.0.
 *
 * Published by the counter_service example plugin with sco_api's provide_service. Another
 * plugin includes this header (copy it into your own project), names the service in its
 * plugin.ini (`requires = counter_service.counter`) and finds the table with query_service.
 * counter_panel is that consumer.
 *
 * The rules that keep the table stable are sco_api.h's:
 *  - The table starts with uint32_t size, set by the provider to sizeof(counter_service_v1)
 *    as it was built. A consumer checks that size covers a member before reading or calling
 *    it (COUNTER_SERVICE_HAS below), so a 1.0 provider and a 1.1 consumer still work together.
 *  - Version 1 only grows at the end: a later minor appends members after get and adds a
 *    COUNTER_SERVICE_VERSION_1_<minor> constant. Nothing is removed, renamed, retyped or
 *    reordered. A break needs a new major; to keep 1.x consumers working, publish it under a
 *    second name (for example counter_service.counter2) beside this table, see the plugin
 *    design guide. (A 2.0 table under this name would answer SCO_UNAVAILABLE to every 1.x
 *    consumer.)
 *  - The layout is pinned below with static asserts, so an accidental change fails the build.
 *  - Plain C types only: no C++ types, no exceptions, no varargs across the boundary.
 *
 * Threads: call add and get on the game thread only (in a command, a task from
 * run_on_game_thread, or an event callback such as tick). The provider keeps no lock.
 *
 * Lifetime: the table belongs to the provider. Query it when you need it instead of keeping the
 * pointer, and never call it from your sco_plugin_unload.
 *
 * Part of the sco SDK examples. GPL-3.0, like sco-core.
 */
#ifndef COUNTER_SERVICE_H
#define COUNTER_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "sco_api.h"

#define COUNTER_SERVICE_NAME        "counter_service.counter"
#define COUNTER_SERVICE_VERSION_1_0 0x00010000u /* (major << 16) | minor */

typedef struct counter_service_v1 {
    uint32_t size;  /* sizeof(counter_service_v1) as the provider built it */
    uint32_t _pad0; /* 0 */
    /* Adds delta to the counter and, when out_value is not NULL, writes the new value there.
     * SCO_BAD_ARG: the sum would overflow int64_t; the counter is unchanged. */
    sco_result (*add)(int64_t delta, int64_t* out_value);
    /* Writes the counter to *out_value. SCO_BAD_ARG: out_value is NULL. */
    sco_result (*get)(int64_t* out_value);
    /* 1.1 and later: new members go here, at the end. */
} counter_service_v1;

/* Layout pins: size first, members in order, pointer-sized slots (24 bytes on x64). */
#ifdef __cplusplus
#define COUNTER_SERVICE_STATIC_ASSERT(e, m) static_assert(e, m)
#else
#define COUNTER_SERVICE_STATIC_ASSERT(e, m) _Static_assert(e, m)
#endif
COUNTER_SERVICE_STATIC_ASSERT(offsetof(counter_service_v1, size) == 0, "counter_service_v1.size");
COUNTER_SERVICE_STATIC_ASSERT(offsetof(counter_service_v1, add) == 8, "counter_service_v1.add");
COUNTER_SERVICE_STATIC_ASSERT(offsetof(counter_service_v1, get) == 8 + sizeof(void*), "counter_service_v1.get");
COUNTER_SERVICE_STATIC_ASSERT(sizeof(counter_service_v1) == 8 + 2 * sizeof(void*), "counter_service_v1 size");

/* 1 when table is not NULL, its size covers the whole member and the member is not NULL. Check
 * it before every member you call: covering one member says nothing about a later one. */
#define COUNTER_SERVICE_HAS(table, member)                                                  \
    ((table) != NULL &&                                                                     \
     (table)->size >= offsetof(counter_service_v1, member) + sizeof((table)->member) &&     \
     (table)->member != NULL)

#endif /* COUNTER_SERVICE_H */
