/* SPDX-License-Identifier: GPL-3.0-only */
#include <sc_asop_selection.h>
#include <stddef.h>
#ifdef __cplusplus
#define PIN(x) static_assert(x, #x)
#else
#define PIN(x) _Static_assert(x, #x)
#endif
PIN(sizeof(sc_asop_selection_state)==80);
PIN(sizeof(sc_asop_selection_read)==32);
PIN(sizeof(sc_asop_selection_row)==72);
PIN(sizeof(sc_asop_selection_class)==72);
PIN(sizeof(sc_asop_selection_request)==80);
PIN(sizeof(sc_asop_selection_receipt)==48);
PIN(sizeof(sc_asop_selection_v1)==64);
PIN(offsetof(sc_asop_selection_v1,state)==8);
PIN(offsetof(sc_asop_selection_v1,refresh)==16);
PIN(offsetof(sc_asop_selection_v1,rows)==24);
PIN(offsetof(sc_asop_selection_v1,acquire)==32);
PIN(offsetof(sc_asop_selection_v1,release)==40);
PIN(offsetof(sc_asop_selection_v1,replace)==48);
PIN(offsetof(sc_asop_selection_v1,status)==56);
PIN(SC_ASOP_SELECTION_VERSION==0x00010000u);
PIN(SC_ASOP_SELECTION_ROW_BUSY==0x01u);
PIN(SC_ASOP_SELECTION_REASON_BUSY==5u);
