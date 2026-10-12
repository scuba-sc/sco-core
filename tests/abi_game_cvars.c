/* SPDX-License-Identifier: GPL-3.0-only */
#include <sc_cvars.h>
#include <stddef.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sco_api) == 128, "frozen root");
PIN(sizeof(sc_cvars_v1) == 16, "cvars table");
PIN(offsetof(sc_cvars_v1, size) == 0, "size prefix");
PIN(offsetof(sc_cvars_v1, get_bool) == 8, "get slot");
PIN(sizeof(((sc_cvars_v1*)0)->get_bool) == 8, "get pointer");
PIN(sizeof(sco_result) == 4, "fixed result");
