/* SPDX-License-Identifier: GPL-3.0-only
 * Separate opt-in reversible bool writes; no arbitrary console/config access.
 * Exact names/types match sc_cvars.h. One owner per name; first prior value is
 * retained until explicit restore, ordinary owner release, session exit or stop.
 * Restore failure retains ownership/context and requires process restart.
 */
#ifndef SC_CVARS_WRITE_H
#define SC_CVARS_WRITE_H
#include "sc_cvars.h"
#define SC_CVARS_WRITE_NAME "game.cvars_write"
#define SC_CVARS_WRITE_VERSION 0x00010000u
typedef struct sc_cvars_write_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Game thread; live self; SC_CVAR_BOOL and canonical 0/1 only. */
    sco_result (*set_bool)(sco_plugin* self, const char* name, uint32_t type, uint32_t value);
    /* Restore this owner's first prior values; no-op OK when none are owned. */
    sco_result (*restore_all)(sco_plugin* self);
} sc_cvars_write_v1;
#endif
