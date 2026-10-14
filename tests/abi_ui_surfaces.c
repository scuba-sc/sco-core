/* SPDX-License-Identifier: GPL-3.0-only
 * abi_ui_surfaces.c: pins the layout of sco_ui_surfaces.h (the host service "sco.ui.surfaces", 1.0).
 *
 * Compile-only, like abi_v1.c: if anything here fails, the service table or a payload struct
 * changed and every plugin built against the old header would break. Version 1 only grows at
 * the end of each struct; existing lines never change.
 *
 * tools/test.sh compiles it as C11 and C++20 for the host, with -fshort-enums, and for
 * x86_64-pc-windows-msvc; the CMake test abi_ui_surfaces rebuilds it.
 */
#include <stddef.h>
#include <stdint.h>

#include "sco_ui_surfaces.h"

#ifdef __cplusplus
#define PIN(e) static_assert(e, #e)
#define ALIGN(t) alignof(t)
#else
#define PIN(e) _Static_assert(e, #e)
#define ALIGN(t) _Alignof(t)
#endif
#define AT(t, f, n) PIN(offsetof(t, f) == (n))

PIN(sizeof(void*) == 8);

/* ---- name, version, limits, discriminants ---- */
PIN(SCO_UI_SURFACES_VERSION_1_0 == 0x00010000u);
PIN(sizeof(SCO_UI_SURFACES_NAME) == 16); /* "sco.ui.surfaces" */
PIN(SCO_UI_SURFACES_MAX_PER_PLUGIN == 32u);
PIN(SCO_UI_SURFACES_MAX_TOTAL == 256u);
PIN(SCO_UI_SURFACES_MAX_PANEL_ID == 63u);
/* Floating limits: C11 integer constant expressions allow a floating constant only as the
 * direct operand of a cast. */
PIN((long long)SCO_UI_SURFACES_MAX_EXTENT_M == 1000);
PIN((long long)SCO_UI_SURFACES_MAX_DISTANCE_M == 10000000);
PIN((long long)SCO_UI_SURFACES_MAX_COORD_M == 1000000000000LL);
#ifdef __cplusplus
PIN(SCO_UI_SURFACES_UNIT_TOLERANCE == 1.0e-3);
#endif
PIN(SCO_SURFACE_HUD_2D == 1u);
PIN(SCO_SURFACE_BILLBOARD_3D == 2u);
PIN(SCO_SURFACE_QUAD_3D == 3u);
PIN(SCO_SURFACE_OCCLUDE_RAYCAST == 0x1u);

/* ---- sco_surface_pose ---- */
PIN(sizeof(sco_surface_pose) == 112);
PIN(ALIGN(sco_surface_pose) == 8);
AT(sco_surface_pose, size, 0);
AT(sco_surface_pose, flags, 4);
AT(sco_surface_pose, pos, 8);
AT(sco_surface_pose, normal, 32);
AT(sco_surface_pose, up, 56);
AT(sco_surface_pose, width_m, 80);
AT(sco_surface_pose, height_m, 88);
AT(sco_surface_pose, max_distance_m, 96);
AT(sco_surface_pose, panel_id, 104);

/* ---- sco_surface_desc ---- */
PIN(sizeof(sco_surface_desc) == 16);
PIN(ALIGN(sco_surface_desc) == 8);
AT(sco_surface_desc, size, 0);
AT(sco_surface_desc, kind, 4);
AT(sco_surface_desc, pose, 8);

/* ---- sco_ui_surfaces_v1 ---- */
PIN(sizeof(sco_ui_surfaces_v1) == 40);
PIN(ALIGN(sco_ui_surfaces_v1) == 8);
AT(sco_ui_surfaces_v1, size, 0);
AT(sco_ui_surfaces_v1, _pad, 4);
AT(sco_ui_surfaces_v1, register_surface, 8);
AT(sco_ui_surfaces_v1, update_surface_pose, 16);
AT(sco_ui_surfaces_v1, unregister_surface, 24);
AT(sco_ui_surfaces_v1, last_error, 32);

/* ---- signatures: a changed parameter list fails to convert ---- */
static void pin_ui_surfaces_signatures(const sco_ui_surfaces_v1* t) {
    sco_result (*register_surface)(sco_plugin*, const sco_surface_desc*, uint32_t*) = t->register_surface;
    sco_result (*update_surface_pose)(sco_plugin*, uint32_t, const sco_surface_pose*) = t->update_surface_pose;
    sco_result (*unregister_surface)(sco_plugin*, uint32_t) = t->unregister_surface;
    sco_result (*last_error)(sco_plugin*, char*, uint32_t*) = t->last_error;
    (void)register_surface; (void)update_surface_pose; (void)unregister_surface; (void)last_error;
}

/* Keeps the pins referenced so -Wunused does not fire. */
void sco_abi_ui_surfaces_pins(void);
void sco_abi_ui_surfaces_pins(void) { (void)&pin_ui_surfaces_signatures; }
