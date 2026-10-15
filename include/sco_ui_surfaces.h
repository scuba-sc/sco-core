/* SPDX-License-Identifier: GPL-3.0-only
 * sco_ui_surfaces.h: the host service "sco.ui.surfaces", version 1.0.
 *
 * Plugins say WHERE a sco.widgets panel is shown: as a 2D HUD rectangle, as a camera-facing 3D
 * billboard, or as a world-anchored 3D quad. Plain C; usable from C and C++. Published by the
 * host (a host-owned service under the reserved id "sco"), found with sco_api 1.1 query_service:
 *
 *   const sco_ui_surfaces_v1* s = NULL;
 *   if (api->size >= offsetof(sco_api, query_service) + sizeof(api->query_service) &&
 *       api->query_service(SCO_UI_SURFACES_NAME, SCO_UI_SURFACES_VERSION_1_0,
 *                          (const void**)&s) == SCO_OK) {
 *       sco_surface_pose pose = { sizeof(sco_surface_pose) };
 *       pose.pos[0] = 10.0; pose.up[2] = 1.0; pose.width_m = 2.0; pose.height_m = 1.0;
 *       pose.panel_id = "hello.main";
 *       sco_surface_desc desc = { sizeof(sco_surface_desc), SCO_SURFACE_BILLBOARD_3D, &pose };
 *       uint32_t id = 0;
 *       s->register_surface(self, &desc, &id);
 *   }
 *
 * The split: sco-core keeps the validated registry (owner, kind, pose) and draws nothing; the
 * product's renderer reads it (sco/surfaces.h), projects each surface and draws the panel's
 * retained widget tree into it. The projection math is documented in docs/ui.md ("Surfaces").
 *
 * Reference: docs/ui.md. Layout pinned by tests/abi_ui_surfaces.c. The rules of sco_api.h hold
 * here too (results instead of exceptions, 64-bit only), plus:
 *  - Discriminants and flags are fixed-width uint32_t macros, never compiler-sized enums.
 *  - Every size field must cover the complete 1.0 struct; larger tails are ignored.
 *  - Coordinates are the product's world frame in metres, as doubles. Every double must be
 *    finite. A "unit" vector has length 1 within SCO_UI_SURFACES_UNIT_TOLERANCE; a "zero"
 *    vector is exactly (0, 0, 0).
 *  - Per kind (anything else is SCO_BAD_ARG):
 *      HUD_2D:       pos[0], pos[1] = the rectangle's centre in viewport fractions [0, 1]
 *                    (origin top-left), pos[2] = 0; width_m, height_m = viewport fractions in
 *                    (0, 1]; normal and up zero; max_distance_m 0; no flags.
 *      BILLBOARD_3D: pos = the centre in world metres (|each| <= SCO_UI_SURFACES_MAX_COORD_M);
 *                    normal zero (the renderer turns it toward the camera); up unit (the world
 *                    direction that is "up" on the panel); width_m, height_m in
 *                    (0, SCO_UI_SURFACES_MAX_EXTENT_M]; max_distance_m in
 *                    [0, SCO_UI_SURFACES_MAX_DISTANCE_M], 0 = no limit.
 *      QUAD_3D:      as BILLBOARD_3D, but normal is a unit vector (the visible face points along
 *                    it) and up is a unit vector perpendicular to it (|normal . up| within the
 *                    tolerance).
 *    flags: 0 or SCO_SURFACE_OCCLUDE_RAYCAST (3D kinds only: the renderer hides the surface when
 *    world geometry lies between the camera and it).
 *  - panel_id: the sco.widgets panel shown on the surface, "<plugin id>.<name>" under the
 *    caller's own id ([a-z0-9_.], at most 63 bytes). It is copied; the panel need not exist yet
 *    (the renderer shows nothing until it does).
 *  - Surface ids are host-assigned, nonzero and never reused while the host runs. At most
 *    SCO_UI_SURFACES_MAX_PER_PLUGIN surfaces per plugin, SCO_UI_SURFACES_MAX_TOTAL in all.
 *  - Any thread. No callbacks: nothing of the plugin's is borrowed after a call returns.
 *  - When the plugin unloads or crashes, every surface it registered is withdrawn; every later
 *    call naming that self is SCO_BAD_ARG. Once the host has stopped the service, calls answer
 *    SCO_UNAVAILABLE.
 *  - A refused call leaves a message for last_error, such as "surface 7: up is not a unit vector".
 *
 * License: GPL-3.0, like the rest of sco-core.
 */
#ifndef SCO_UI_SURFACES_H
#define SCO_UI_SURFACES_H

#include "sco_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCO_UI_SURFACES_NAME "sco.ui.surfaces"
#define SCO_UI_SURFACES_VERSION_1_0 0x00010000u

#define SCO_UI_SURFACES_MAX_PER_PLUGIN 32u     /* live surfaces of one plugin */
#define SCO_UI_SURFACES_MAX_TOTAL      256u    /* live surfaces, every plugin together */
#define SCO_UI_SURFACES_MAX_PANEL_ID   63u     /* panel_id bytes, NUL excluded */
#define SCO_UI_SURFACES_MAX_EXTENT_M   1000.0  /* 3D width_m / height_m */
#define SCO_UI_SURFACES_MAX_DISTANCE_M 1.0e7   /* 3D max_distance_m */
#define SCO_UI_SURFACES_MAX_COORD_M    1.0e12  /* |pos[i]| of a 3D surface */
#define SCO_UI_SURFACES_UNIT_TOLERANCE 1.0e-3  /* | |v| - 1 | and |normal . up| */

/* sco_surface_desc.kind */
#define SCO_SURFACE_HUD_2D       1u
#define SCO_SURFACE_BILLBOARD_3D 2u
#define SCO_SURFACE_QUAD_3D      3u

/* sco_surface_pose.flags */
#define SCO_SURFACE_OCCLUDE_RAYCAST 0x1u

typedef struct sco_surface_pose {
    uint32_t    size;            /* sizeof(sco_surface_pose) */
    uint32_t    flags;           /* SCO_SURFACE_OCCLUDE_RAYCAST or 0 */
    double      pos[3];
    double      normal[3];
    double      up[3];
    double      width_m;
    double      height_m;
    double      max_distance_m;  /* 0: no limit */
    const char* panel_id;        /* the sco.widgets panel shown; copied */
} sco_surface_pose;

typedef struct sco_surface_desc {
    uint32_t                size;   /* sizeof(sco_surface_desc) */
    uint32_t                kind;   /* SCO_SURFACE_*; fixed for the surface's life */
    const sco_surface_pose* pose;   /* read during the call only */
} sco_surface_desc;

typedef struct sco_ui_surfaces_v1 {
    uint32_t size; /* sizeof(sco_ui_surfaces_v1) as the host built it */
    uint32_t _pad;

    /* Adds a surface of desc->kind at desc->pose and writes its id to *out_id. SCO_BAD_ARG: a NULL
     * or short desc/pose, an unknown kind, any field outside the rules above, out_id NULL.
     * SCO_TOO_MANY: this plugin's or the host's surface limit is reached. Nothing changes and
     * *out_id is untouched unless SCO_OK. */
    sco_result (*register_surface)(sco_plugin* self, const sco_surface_desc* desc, uint32_t* out_id);
    /* Replaces the pose (and panel) of one of this plugin's surfaces, checked against its kind.
     * SCO_NOT_FOUND: this plugin has no surface by that id. SCO_BAD_ARG: as for register_surface.
     * Failure leaves the old pose. */
    sco_result (*update_surface_pose)(sco_plugin* self, uint32_t id, const sco_surface_pose* pose);
    /* Removes one of this plugin's surfaces. SCO_NOT_FOUND: it has none by that id. */
    sco_result (*unregister_surface)(sco_plugin* self, uint32_t id);
    /* The message of this plugin's last refused call ("" if none), NUL-terminated, with the
     * raw-handler size handshake (*inout_size: capacity in; bytes written, or needed with
     * SCO_TOO_MANY, out). */
    sco_result (*last_error)(sco_plugin* self, char* out, uint32_t* inout_size);
} sco_ui_surfaces_v1;

#ifdef __cplusplus
}
#endif

#endif /* SCO_UI_SURFACES_H */
