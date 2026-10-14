/*
 * abi_game_hangar.c: pins the layout of sc_hangar.h (the game pack's service "game.hangar", 1.0).
 *
 * Compile-only, like abi_v1.c: if anything here fails, the service table changed and every
 * plugin built against the old header would break. Version 1 only grows at the end of
 * sc_hangar_v1; existing lines never change.
 *
 * CTest (abi_game_hangar) compiles it as C11 and C++20, plus -fshort-enums where the compiler
 * has it.
 */
#include <stddef.h>
#include <stdint.h>

#include "sc_hangar.h"

#ifdef __cplusplus
#define PIN(expr) static_assert(expr, #expr)
#else
#define PIN(expr) _Static_assert(expr, #expr)
#endif

#define SIZE(T, n)      PIN(sizeof(T) == (n))
#define AT(T, f, n)     PIN(offsetof(T, f) == (n))

PIN(sizeof(void*) == 8);

/* ---- name, version ---- */
PIN(SC_HANGAR_SERVICE_VERSION == 0x00010000u);
PIN(sizeof(SC_HANGAR_SERVICE_NAME) == 12); /* "game.hangar" */

/* ---- sc_hangar_lift_op: 4 bytes, even with -fshort-enums ---- */
SIZE(sc_hangar_lift_op, 4);
PIN(SC_HANGAR_LIFT_OPEN == 1);
PIN(SC_HANGAR_LIFT_CLOSE == 2);
PIN(SC_HANGAR_LIFT_OP_FORCE32 == 0x7fffffff);

/* ---- sc_hangar_v1 ---- */
SIZE(sc_hangar_v1, 40);
AT(sc_hangar_v1, size, 0);
AT(sc_hangar_v1, _pad, 4);
AT(sc_hangar_v1, lift_of_landing_area, 8);
AT(sc_hangar_v1, lift_request, 16);
AT(sc_hangar_v1, atc_cancel_request, 24);
AT(sc_hangar_v1, last_error, 32);

/* ---- signatures: a changed parameter list fails to convert ---- */
static void pin_hangar_signatures(const sc_hangar_v1* h) {
    sco_result (*lift_of_landing_area)(sco_plugin*, uint64_t, uint64_t*) = h->lift_of_landing_area;
    sco_result (*lift_request)(sco_plugin*, uint64_t, uint32_t) = h->lift_request;
    sco_result (*atc_cancel_request)(sco_plugin*, uint64_t) = h->atc_cancel_request;
    sco_result (*last_error)(sco_plugin*, char*, uint32_t*) = h->last_error;
    (void)lift_of_landing_area; (void)lift_request; (void)atc_cancel_request; (void)last_error;
}

/* Keeps the pins referenced so -Wunused does not fire. */
void sco_abi_game_hangar_pins(void);
void sco_abi_game_hangar_pins(void) { (void)&pin_hangar_signatures; }
