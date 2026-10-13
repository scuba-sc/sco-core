/* GPL-3.0-only. Independent C/C++ x64 layout pins for source ingress 1.0. */
#include "sco_diagnostics_publisher.h"
#include <stddef.h>
#ifdef __cplusplus
#define PIN(x) static_assert(x, #x)
#else
#define PIN(x) _Static_assert(x, #x)
#endif
PIN(sizeof(void*) == 8);
PIN(sizeof(sco_diagnostics_health) == 4);
PIN(SCO_DIAGNOSTICS_HEALTH_UNKNOWN == 0);
PIN(SCO_DIAGNOSTICS_HEALTH_READY == 1);
PIN(SCO_DIAGNOSTICS_HEALTH_DEGRADED == 2);
PIN(SCO_DIAGNOSTICS_PUBLISHER_VERSION_1_0 == 0x00010000u);
PIN(sizeof(sco_diagnostics_observation_v1) == 8);
PIN(offsetof(sco_diagnostics_observation_v1, size) == 0);
PIN(offsetof(sco_diagnostics_observation_v1, health) == 4);
PIN(sizeof(sco_diagnostics_publisher_v1) == 16);
PIN(offsetof(sco_diagnostics_publisher_v1, size) == 0);
PIN(offsetof(sco_diagnostics_publisher_v1, _pad) == 4);
PIN(offsetof(sco_diagnostics_publisher_v1, submit) == 8);
void sco_abi_diagnostics_publisher_pins(void);
void sco_abi_diagnostics_publisher_pins(void) {
    sco_result (*submit)(sco_plugin*, const sco_diagnostics_observation_v1*) = ((sco_diagnostics_publisher_v1*)0)->submit;
    (void)submit;
}
#undef PIN
