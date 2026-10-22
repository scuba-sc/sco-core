/* SPDX-License-Identifier: GPL-3.0-only */
#include "sc_input.h"
#include <stddef.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sc_game_action_id) == 4, "action id width");
PIN(sizeof(sc_input_action_info) == 108, "copied action record");
PIN(sizeof(sc_input_v1) == 32, "input table extent");
PIN(SC_INPUT_ACTION_COUNT == 1297u, "4.10.197.2878 action count");
PIN(SC_ACTION_INVALID == 0, "no-action value");
PIN(SC_ACTION_BUILDMODE_TOGGLE_CONSTRUCTION_MENU == 1297, "last action id");
PIN(offsetof(sc_input_v1, count) == 8, "count prefix");
PIN(offsetof(sc_input_v1, by_name) == 24, "lookup tail");
int main(void) { return 0; }
