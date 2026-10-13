/* GPL-3.0-only. Independent x64 pins, C11/C++20/short-enum/Windows. */
#include <stddef.h>
#include "sc_prefabs.h"
#ifdef __cplusplus
#define PIN(x) static_assert(x, #x)
#else
#define PIN(x) _Static_assert(x, #x)
#endif
PIN(sizeof(sc_prefabs_v1) == 16);
PIN(offsetof(sc_prefabs_v1, size) == 0);
PIN(offsetof(sc_prefabs_v1, _pad) == 4);
PIN(offsetof(sc_prefabs_v1, spawn) == 8);
PIN(sizeof(SC_PREFABS_NAME) == 13);
PIN(SC_PREFABS_VERSION_1_0 == 0x00010000u);
PIN(SC_PREFABS_MAX_PATH == 511u);
PIN(SC_PREFABS_MAX_PER_OWNER == 128u);
void sco_abi_prefabs_pins(void);
void sco_abi_prefabs_pins(void) {
    sco_result (*spawn)(sco_plugin*, const char*, uint64_t, const double*, const double*, uint64_t*) = ((sc_prefabs_v1*)0)->spawn;
    (void)spawn;
}
