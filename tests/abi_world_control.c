/* GPL-3.0-only. Independent x64 C/C++ layout pins, synthetic contract. */
#include <stddef.h>
#include <sc_world_control.h>
#ifdef __cplusplus
#define PIN(x) static_assert(x, #x)
#else
#define PIN(x) _Static_assert(x, #x)
#endif
PIN(sizeof(sc_world_control_v1) == 48);
PIN(offsetof(sc_world_control_v1, raycast_filtered_many) == 40);
PIN(sizeof(sc_world_hits) == 4624);
PIN(offsetof(sc_world_hits, hits) == 16);
PIN(SC_WORLD_CONTROL_VERSION_1_1 == 0x00010001u);
PIN(SC_WORLD_CONTROL_MAX_HITS == 64u);
PIN(offsetof(sc_world_control_v1, size) == 0);
PIN(offsetof(sc_world_control_v1, _pad) == 4);
PIN(offsetof(sc_world_control_v1, raycast_filtered) == 8);
PIN(offsetof(sc_world_control_v1, camera_acquire) == 16);
PIN(offsetof(sc_world_control_v1, camera_step) == 24);
PIN(offsetof(sc_world_control_v1, camera_release) == 32);
PIN(sizeof(sc_world_camera_step) == 64);
PIN(offsetof(sc_world_camera_step, translation) == 8);
PIN(offsetof(sc_world_camera_step, rotation_xyzw) == 32);
PIN(SC_WORLD_CONTROL_VERSION_1_0 == 0x00010000u);
PIN(SC_WORLD_CONTROL_MAX_SKIPS == 32u);
/* C11 assertions require an integer constant expression. Numeric double
 * limits are additionally checked by the C++ build and synthetic runtime. */
#ifdef __cplusplus
PIN(SC_WORLD_CONTROL_STEP_METRES == 10.0);
PIN(SC_WORLD_CONTROL_STEP_RADIANS == 0.25);
#endif
void sco_abi_world_control_pins(void);
void sco_abi_world_control_pins(void) {
    const sc_world_control_v1* t = 0;
    sco_result (*ray)(sco_plugin*, uint64_t, const double*, const double*, double, const uint64_t*, uint32_t, sc_world_hit*) = t->raycast_filtered;
    sco_result (*acquire)(sco_plugin*, uint64_t*) = t->camera_acquire;
    sco_result (*step)(sco_plugin*, uint64_t, const sc_world_camera_step*) = t->camera_step;
    sco_result (*release)(sco_plugin*, uint64_t) = t->camera_release;
    (void)ray; (void)acquire; (void)step; (void)release;
}
