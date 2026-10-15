/* SPDX-License-Identifier: GPL-3.0-only */
#include "sc_loadout.h"
#include <stddef.h>
#ifdef __cplusplus
#define PIN(c) static_assert(c, #c)
#else
#define PIN(c) _Static_assert(c, #c)
#endif
PIN(sizeof(void*) == 8);
PIN(sizeof(sc_loadout_slot) == 4);
PIN(sizeof(sc_loadout_status) == 4);
PIN(SC_LOADOUT_SERVICE_VERSION == 0x00010000u);
PIN(SC_LOADOUT_VERSION_1_0 == 0x00010000u);
PIN(SC_LOADOUT_VERSION_1_1 == 0x00010001u);
PIN(SC_LOADOUT_MAX_ITEMS == 16u);
PIN(SC_LOADOUT_ITEM_NAME_BYTES == 96u);
PIN(SC_LOADOUT_SLOT_HEAD == 1 && SC_LOADOUT_SLOT_TORSO == 2 && SC_LOADOUT_SLOT_ARMS == 3);
PIN(SC_LOADOUT_SLOT_LEGS == 4 && SC_LOADOUT_SLOT_UNDERSUIT == 5 && SC_LOADOUT_SLOT_BACKPACK == 6 && SC_LOADOUT_SLOT_VISOR == 7);
PIN(SC_LOADOUT_SLOT_PRIMARY == 8);
PIN(SC_LOADOUT_SLOT_SIDEARM == 9);
PIN(SC_LOADOUT_SLOT_PRIMARY_OPTIC == 10);
PIN(SC_LOADOUT_SLOT_PRIMARY_BARREL == 11);
PIN(SC_LOADOUT_SLOT_PRIMARY_UNDERBARREL == 12);
PIN(SC_LOADOUT_SLOT_SIDEARM_OPTIC == 13);
PIN(SC_LOADOUT_SLOT_SIDEARM_BARREL == 14);
PIN(SC_LOADOUT_SLOT_SIDEARM_UNDERBARREL == 15);
PIN(SC_LOADOUT_SLOT_PRIMARY_TOOL_MODULE == 16);
PIN(SC_LOADOUT_SLOT_PRIMARY_TOOL_CANISTER == 17);
PIN(SC_LOADOUT_SLOT_PRIMARY_TOOL_BATTERY == 18);
PIN(SC_LOADOUT_SLOT_SIDEARM_TOOL_MODULE == 19);
PIN(SC_LOADOUT_SLOT_SIDEARM_TOOL_CANISTER == 20);
PIN(SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY == 21);
PIN(SC_LOADOUT_APPLIED == 0 && SC_LOADOUT_UNSUPPORTED == 1 && SC_LOADOUT_ITEM_MISSING == 2);
PIN(SC_LOADOUT_FAILED == 3 && SC_LOADOUT_UNAVAILABLE == 4);
PIN(sizeof(sc_loadout_item) == 104);
PIN(offsetof(sc_loadout_item, slot) == 0);
PIN(offsetof(sc_loadout_item, _pad) == 4);
PIN(offsetof(sc_loadout_item, item_class) == 8);
PIN(sizeof(sc_loadout_slot_result) == 8);
PIN(offsetof(sc_loadout_slot_result, slot) == 0);
PIN(offsetof(sc_loadout_slot_result, status) == 4);
PIN(sizeof(sc_loadout_request) == 24);
PIN(offsetof(sc_loadout_request, size) == 0);
PIN(offsetof(sc_loadout_request, count) == 4);
PIN(offsetof(sc_loadout_request, item_stride) == 8);
PIN(offsetof(sc_loadout_request, _pad) == 12);
PIN(offsetof(sc_loadout_request, items) == 16);
PIN(sizeof(sc_loadout_report) == 144);
PIN(offsetof(sc_loadout_report, size) == 0);
PIN(offsetof(sc_loadout_report, count) == 4);
PIN(offsetof(sc_loadout_report, applied) == 8);
PIN(offsetof(sc_loadout_report, _pad) == 12);
PIN(offsetof(sc_loadout_report, slots) == 16);
PIN(sizeof(sc_loadout_v1) == 16);
PIN(offsetof(sc_loadout_v1, size) == 0);
PIN(offsetof(sc_loadout_v1, _pad) == 4);
PIN(offsetof(sc_loadout_v1, apply) == 8);
PIN(offsetof(sc_loadout_v1, apply) + sizeof(((sc_loadout_v1*)0)->apply) == 16);
