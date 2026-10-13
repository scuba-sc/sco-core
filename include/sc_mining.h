/* SPDX-License-Identifier: GPL-3.0-only
 * SPDX-FileCopyrightText: 2026 the sco-core contributors
 * Off by default. Game-thread methods; worker threads never call plugins.
 * request accepts owner intent (SCO_OK), not engine acceptance. state copies
 * owner-scoped requested/active plus process counters; block explains refusal. */
#ifndef SC_MINING_H
#define SC_MINING_H
#include "sco_api.h"
#include <stddef.h>
#define SC_MINING_SERVICE_NAME "game.mining"
#define SC_MINING_SERVICE_VERSION 0x00010000u
#define SC_MINING_BLOCK_OFF 0u
#define SC_MINING_BLOCK_UNAVAILABLE 1u
#define SC_MINING_BLOCK_ADMISSION 2u
#define SC_MINING_BLOCK_RESTART 3u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sc_mining_state {
    uint32_t size;
    uint32_t requested;
    uint32_t active;
    uint32_t block;
    uint64_t cells;
    uint64_t promoted; /* forwarded spawning bit, not harvested entities */
    uint64_t faults;
} sc_mining_state;
typedef struct sc_mining_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*request)(sco_plugin* self, uint32_t enabled);
    sco_result (*state)(sco_plugin* self, sc_mining_state* out);
} sc_mining_v1;
#define SC_MINING_HAS(t, member) ((t) && (t)->size >= offsetof(sc_mining_v1, member) + sizeof((t)->member) && (t)->member)
static inline sco_result sc_mining_request(const sc_mining_v1* t, sco_plugin* self, uint32_t enabled) {
    return SC_MINING_HAS(t, request) ? t->request(self, enabled) : SCO_UNAVAILABLE;
}
static inline sco_result sc_mining_read_state(const sc_mining_v1* t, sco_plugin* self, sc_mining_state* out) {
    return SC_MINING_HAS(t, state) ? t->state(self, out) : SCO_UNAVAILABLE;
}
#undef SC_MINING_HAS
#ifdef __cplusplus
}
#endif
#endif
