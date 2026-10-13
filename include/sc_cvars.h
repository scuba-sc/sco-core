/* SPDX-License-Identifier: GPL-3.0-only
 * Scoped SQ42 bool observations, not plugin settings or arbitrary console access.
 * Game thread only; valid live self, verified offline adapter and capability required.
 * Outputs unchanged on failure. Borrowed table remains address-stable but unusable
 * after stop; query again on every load. All calls are non-reentrant.
 */
#ifndef SC_CVARS_H
#define SC_CVARS_H
#include "sco_api.h"
#define SC_CVARS_NAME "game.cvars"
#define SC_CVARS_VERSION 0x00010000u
#define SC_CVAR_BOOL 1u
typedef struct sc_cvars_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Exact case-sensitive name; type must be SC_CVAR_BOOL; out is copied 0 or 1. */
    sco_result (*get_bool)(sco_plugin* self, const char* name, uint32_t type, uint32_t* out);
} sc_cvars_v1;
#endif
