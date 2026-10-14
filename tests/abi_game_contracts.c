/*
 * abi_game_contracts.c: pins the layout of sc_contracts.h (the game pack's service
 * "game.contracts", 1.0).
 *
 * Compile-only, like abi_v1.c: if anything here fails, the service table changed and every
 * plugin built against the old header would break. Version 1 only grows at the end of
 * sc_contracts_v1; existing lines never change.
 *
 * CTest (abi_game_contracts) compiles it as C11 and C++20, plus -fshort-enums where the compiler
 * has it.
 */
#include <stddef.h>
#include <stdint.h>

#include "sc_contracts.h"

#ifdef __cplusplus
#define PIN(expr) static_assert(expr, #expr)
#else
#define PIN(expr) _Static_assert(expr, #expr)
#endif

#define SIZE(T, n)      PIN(sizeof(T) == (n))
#define AT(T, f, n)     PIN(offsetof(T, f) == (n))

PIN(sizeof(void*) == 8);
PIN(sizeof(float) == 4);

/* ---- name, version ---- */
PIN(SC_CONTRACTS_SERVICE_VERSION == 0x00010000u);
PIN(sizeof(SC_CONTRACTS_SERVICE_NAME) == 15); /* "game.contracts" */

/* ---- sc_contracts_v1 ---- */
SIZE(sc_contracts_v1, 24);
AT(sc_contracts_v1, size, 0);
AT(sc_contracts_v1, _pad, 4);
AT(sc_contracts_v1, stream_radius, 8);
AT(sc_contracts_v1, last_error, 16);

/* ---- signatures: a changed parameter list fails to convert ---- */
static void pin_contracts_signatures(const sc_contracts_v1* c) {
    sco_result (*stream_radius)(sco_plugin*, float*) = c->stream_radius;
    sco_result (*last_error)(sco_plugin*, char*, uint32_t*) = c->last_error;
    (void)stream_radius; (void)last_error;
}

/* Keeps the pins referenced so -Wunused does not fire. */
void sco_abi_game_contracts_pins(void);
void sco_abi_game_contracts_pins(void) { (void)&pin_contracts_signatures; }
