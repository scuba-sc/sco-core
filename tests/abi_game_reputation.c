/*
 * abi_game_reputation.c: pins the layout of sc_reputation.h (the game pack's service
 * "game.reputation", 1.0).
 *
 * Compile-only, like abi_v1.c: if anything here fails, the service table changed and every
 * plugin built against the old header would break. Version 1 only grows at the end of
 * sc_reputation_v1; existing lines never change.
 *
 * CTest (abi_game_reputation) compiles it as C11 and C++20, plus -fshort-enums where the compiler
 * has it.
 */
#include <stddef.h>
#include <stdint.h>

#include "sc_reputation.h"

#ifdef __cplusplus
#define PIN(expr) static_assert(expr, #expr)
#else
#define PIN(expr) _Static_assert(expr, #expr)
#endif

#define SIZE(T, n)      PIN(sizeof(T) == (n))
#define AT(T, f, n)     PIN(offsetof(T, f) == (n))

PIN(sizeof(void*) == 8);

/* ---- name, version ---- */
PIN(SC_REPUTATION_SERVICE_VERSION == 0x00010000u);
PIN(sizeof(SC_REPUTATION_SERVICE_NAME) == 16); /* "game.reputation" */

/* ---- sc_reputation_checks ---- */
SIZE(sc_reputation_checks, 16);
AT(sc_reputation_checks, size, 0);
AT(sc_reputation_checks, sites, 4);
AT(sc_reputation_checks, intact, 8);
AT(sc_reputation_checks, services, 12);

/* ---- sc_reputation_v1 ---- */
SIZE(sc_reputation_v1, 24);
AT(sc_reputation_v1, size, 0);
AT(sc_reputation_v1, _pad, 4);
AT(sc_reputation_v1, checks, 8);
AT(sc_reputation_v1, last_error, 16);

/* ---- signatures: a changed parameter list fails to convert ---- */
static void pin_reputation_signatures(const sc_reputation_v1* r) {
    sco_result (*checks)(sco_plugin*, sc_reputation_checks*) = r->checks;
    sco_result (*last_error)(sco_plugin*, char*, uint32_t*) = r->last_error;
    (void)checks; (void)last_error;
}

/* Keeps the pins referenced so -Wunused does not fire. */
void sco_abi_game_reputation_pins(void);
void sco_abi_game_reputation_pins(void) { (void)&pin_reputation_signatures; }
