/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>
#include "sco_storage_v2.h"

#ifdef __cplusplus
#define PIN(expr) static_assert(expr, #expr)
#else
#define PIN(expr) _Static_assert(expr, #expr)
#endif
#define SIZE(T, n) PIN(sizeof(T) == (n))
#define AT(T, f, n) PIN(offsetof(T, f) == (n))

SIZE(void*, 8);
SIZE(sco_result, 4);
PIN(SCO_STORAGE_V2_VERSION_2_0 == 0x00020000u);
PIN(sizeof(SCO_STORAGE_V2_NAME) == 21);
PIN(SCO_STORAGE_OWNER_OPENED == 1u);
SIZE(sco_storage_owner_info, 32);
AT(sco_storage_owner_info, size, 0);
AT(sco_storage_owner_info, flags, 4);
AT(sco_storage_owner_info, session_generation, 8);
AT(sco_storage_owner_info, table_count, 16);
AT(sco_storage_owner_info, cursor_count, 24);
SIZE(sco_storage_table_info, 16);
AT(sco_storage_table_info, size, 0);
AT(sco_storage_table_info, flags, 4);
AT(sco_storage_table_info, column_count, 8);
SIZE(sco_storage_quota_info, 40);
AT(sco_storage_quota_info, size, 0);
AT(sco_storage_quota_info, flags, 4);
AT(sco_storage_quota_info, configured_bytes, 8);
AT(sco_storage_quota_info, effective_bytes, 16);
AT(sco_storage_quota_info, allocated_bytes, 24);
AT(sco_storage_quota_info, free_bytes, 32);
SIZE(sco_storage_v2, 40);
AT(sco_storage_v2, size, 0);
AT(sco_storage_v2, _pad, 4);
AT(sco_storage_v2, owner_info, 8);
AT(sco_storage_v2, next_table, 16);
AT(sco_storage_v2, row_count, 24);
AT(sco_storage_v2, quota_info, 32);

static void pin_storage_v2_signatures(const sco_storage_v2* s) {
    sco_result (*owner)(sco_plugin*, sco_storage_owner_info*) = s->owner_info;
    sco_result (*next)(sco_plugin*, const char*, char*, uint32_t*, sco_storage_table_info*) = s->next_table;
    sco_result (*rows)(sco_plugin*, const char*, uint64_t*) = s->row_count;
    sco_result (*quota)(sco_plugin*, sco_storage_quota_info*) = s->quota_info;
    (void)owner; (void)next; (void)rows; (void)quota;
}
void sco_abi_storage_v2_pins(void);
void sco_abi_storage_v2_pins(void) { (void)&pin_storage_v2_signatures; }
