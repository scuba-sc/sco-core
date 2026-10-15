/*
 * abi_game_asop.c: pins the layout of sc_asop.h (the game pack's service "game.asop", 1.0).
 *
 * Compile-only, like abi_v1.c: if anything here fails, the service table changed and every
 * plugin built against the old header would break. Version 1 only grows at the end of
 * sc_asop_v1; existing lines never change.
 *
 * CTest (abi_game_asop) compiles it as C11 and C++20, plus -fshort-enums where the compiler
 * has it.
 */
#include <stddef.h>
#include <stdint.h>

#include "sc_asop.h"

#ifdef __cplusplus
#define PIN(expr) static_assert(expr, #expr)
#else
#define PIN(expr) _Static_assert(expr, #expr)
#endif

#define SIZE(T, n)      PIN(sizeof(T) == (n))
#define AT(T, f, n)     PIN(offsetof(T, f) == (n))

PIN(sizeof(void*) == 8);

/* ---- name, version ---- */
PIN(SC_ASOP_SERVICE_VERSION == 0x00010000u);
PIN(sizeof(SC_ASOP_SERVICE_NAME) == 10); /* "game.asop" */

/* ---- sc_asop_v1 ---- */
SIZE(sc_asop_v1, 32);
AT(sc_asop_v1, size, 0);
AT(sc_asop_v1, _pad, 4);
AT(sc_asop_v1, claim_timeout, 8);
AT(sc_asop_v1, shard_persisted, 16);
AT(sc_asop_v1, last_error, 24);

/* ---- signatures: a changed parameter list fails to convert ---- */
static void pin_asop_signatures(const sc_asop_v1* a) {
    sco_result (*claim_timeout)(sco_plugin*, int32_t*) = a->claim_timeout;
    sco_result (*shard_persisted)(sco_plugin*, uint32_t*) = a->shard_persisted;
    sco_result (*last_error)(sco_plugin*, char*, uint32_t*) = a->last_error;
    (void)claim_timeout; (void)shard_persisted; (void)last_error;
}

/* Keeps the pins referenced so -Wunused does not fire. */
void sco_abi_game_asop_pins(void);
void sco_abi_game_asop_pins(void) { (void)&pin_asop_signatures; }
