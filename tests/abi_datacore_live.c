/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sc_datacore_live.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Pin ABI struct sizes and field offsets */
    assert(sizeof(sc_datacore_pool_stats) == 40);
    assert(offsetof(sc_datacore_pool_stats, size) == 0);
    assert(offsetof(sc_datacore_pool_stats, pool_type) == 4);
    assert(offsetof(sc_datacore_pool_stats, element_count) == 8);
    assert(offsetof(sc_datacore_pool_stats, capacity) == 16);
    assert(offsetof(sc_datacore_pool_stats, element_size) == 24);
    assert(offsetof(sc_datacore_pool_stats, total_bytes) == 32);

    assert(sizeof(sc_datacore_live_record_info) == 40);
    assert(offsetof(sc_datacore_live_record_info, size) == 0);
    assert(offsetof(sc_datacore_live_record_info, guid) == 4);
    assert(offsetof(sc_datacore_live_record_info, struct_type) == 20);
    assert(offsetof(sc_datacore_live_record_info, field_count) == 24);
    assert(offsetof(sc_datacore_live_record_info, pool_offset) == 32);

    assert(sizeof(sc_datacore_live_v1) == 48);
    assert(offsetof(sc_datacore_live_v1, size) == 0);
    assert(offsetof(sc_datacore_live_v1, is_ready) == 8);
    assert(offsetof(sc_datacore_live_v1, get_pool_stats) == 16);
    assert(offsetof(sc_datacore_live_v1, find_record) == 24);
    assert(offsetof(sc_datacore_live_v1, read_pool) == 32);
    assert(offsetof(sc_datacore_live_v1, write_pool) == 40);

    puts("PASS abi_datacore_live");
    return 0;
}
