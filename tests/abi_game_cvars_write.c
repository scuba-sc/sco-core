/* SPDX-License-Identifier: GPL-3.0-only */
#include <sc_cvars_write.h>
#include <stddef.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sco_api) == 128, "frozen root");
PIN(sizeof(sc_cvars_v1) == 16, "read table unchanged");
PIN(sizeof(sc_cvars_write_v1) == 24, "write table independent");
PIN(offsetof(sc_cvars_write_v1, size) == 0, "size prefix");
PIN(offsetof(sc_cvars_write_v1, set_bool) == 8, "set slot");
PIN(offsetof(sc_cvars_write_v1, restore_all) == 16, "restore slot");
PIN(sizeof(((sc_cvars_write_v1*)0)->set_bool) == 8, "set pointer");
PIN(sizeof(((sc_cvars_write_v1*)0)->restore_all) == 8, "restore pointer");
PIN(sizeof(sco_result) == 4, "fixed result");
