/*
 * abi_game_creative.c: pins the layout of sc_creative.h (the game pack's service "game.creative", 1.0).
 *
 * Compile-only, like abi_v1.c: if anything here fails, the service table changed and every
 * plugin built against the old header would break. Version 1 only grows at the end of
 * sc_creative_v1; existing lines never change.
 *
 * tools/test.sh compiles it as C11 and C++20 for the host, with -fshort-enums, and for
 * x86_64-pc-windows-msvc; CTest (abi_game_creative) as C11 and C++20, plus -fshort-enums where
 * the compiler has it. The C# SDK's layout test checks its lines against Sco.Sdk.Game.
 */
#include <stddef.h>
#include <stdint.h>

#include "sc_creative.h"

#ifdef __cplusplus
#define PIN(expr) static_assert(expr, #expr)
#else
#define PIN(expr) _Static_assert(expr, #expr)
#endif

#define SIZE(T, n)      PIN(sizeof(T) == (n))
#define AT(T, f, n)     PIN(offsetof(T, f) == (n))

PIN(sizeof(void*) == 8);

/* ---- name, version, limits ---- */
PIN(SC_CREATIVE_SERVICE_VERSION == 0x00010000u);
PIN(sizeof(SC_CREATIVE_SERVICE_NAME) == 14); /* "game.creative" */
PIN(SC_CREATIVE_FLY_SPEED_MAX == 10000);

/* ---- sc_creative_flag: 4 bytes, even with -fshort-enums ---- */
SIZE(sc_creative_flag, 4);
PIN(SC_CREATIVE_GOD_MODE == 0x1);
PIN(SC_CREATIVE_FLY == 0x2);
PIN(SC_CREATIVE_INFINITE_AMMO == 0x4);
PIN(SC_CREATIVE_INFINITE_SHIP_AMMO == 0x8);
PIN(SC_CREATIVE_FLAG_FORCE32 == 0x7fffffff);

/* ---- sc_creative_v1 ---- */
SIZE(sc_creative_v1, 64);
AT(sc_creative_v1, size, 0);
AT(sc_creative_v1, _pad, 4);
AT(sc_creative_v1, set_god_mode, 8);
AT(sc_creative_v1, set_fly_mode, 16);
AT(sc_creative_v1, set_fly_speed, 24);
AT(sc_creative_v1, set_infinite_ammo, 32);
AT(sc_creative_v1, set_infinite_ship_ammo, 40);
AT(sc_creative_v1, state, 48);
AT(sc_creative_v1, last_error, 56);

/* ---- signatures: a changed parameter list fails to convert ---- */
static void pin_creative_signatures(const sc_creative_v1* c) {
    sco_result (*set_god_mode)(sco_plugin*, int) = c->set_god_mode;
    sco_result (*set_fly_mode)(sco_plugin*, int) = c->set_fly_mode;
    sco_result (*set_fly_speed)(sco_plugin*, float) = c->set_fly_speed;
    sco_result (*set_infinite_ammo)(sco_plugin*, int) = c->set_infinite_ammo;
    sco_result (*set_infinite_ship_ammo)(sco_plugin*, int) = c->set_infinite_ship_ammo;
    sco_result (*state)(sco_plugin*, uint32_t*) = c->state;
    sco_result (*last_error)(sco_plugin*, char*, uint32_t*) = c->last_error;
    (void)set_god_mode; (void)set_fly_mode; (void)set_fly_speed; (void)set_infinite_ammo;
    (void)set_infinite_ship_ammo; (void)state; (void)last_error;
}

/* Keeps the pins referenced so -Wunused does not fire. */
void sco_abi_game_creative_pins(void);
void sco_abi_game_creative_pins(void) { (void)&pin_creative_signatures; }
